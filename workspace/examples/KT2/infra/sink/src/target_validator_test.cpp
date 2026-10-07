#include "detection_executor.hpp"
#include "target_validator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function>
void require_throws(Function&& function, const char* message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

radar_sink::TargetDetection detected(double snr_db = 20.0) {
    return {.detected = true, .snr_db = snr_db};
}

radar_sink::TargetDetection reference_detection(
    const std::vector<uestcradar::ComplexFloat32>& bins,
    const radar_sink::TargetConfig& config) {
    radar_sink::TargetDetection result;
    const std::size_t gate_begin = config.range_bin > config.half_width
        ? config.range_bin - config.half_width
        : 0;
    const std::size_t gate_end = std::min(
        bins.size() - 1, config.range_bin + config.half_width);
    const std::size_t noise_begin =
        config.range_bin > config.noise_guard_bins
        ? config.range_bin - config.noise_guard_bins
        : 0;
    const std::size_t noise_end = std::min(
        bins.size() - 1, config.range_bin + config.noise_guard_bins);
    result.target_peak_bin = gate_begin;
    long double background_power = 0.0L;
    std::size_t background_count = 0;
    for (std::size_t index = 0; index < bins.size(); ++index) {
        const float magnitude = std::hypot(bins[index].i, bins[index].q);
        if (magnitude > result.global_peak_magnitude) {
            result.global_peak_magnitude = magnitude;
            result.global_peak_bin = index;
        }
        if (index >= gate_begin && index <= gate_end &&
            magnitude > result.target_peak_magnitude) {
            result.target_peak_magnitude = magnitude;
            result.target_peak_bin = index;
        }
        if (index < noise_begin || index > noise_end) {
            background_power += static_cast<long double>(magnitude) *
                static_cast<long double>(magnitude);
            ++background_count;
        }
    }
    if (background_count == 0 || background_power <= 0.0L) {
        result.snr_db = result.target_peak_magnitude > 0.0F
            ? std::numeric_limits<double>::infinity()
            : -std::numeric_limits<double>::infinity();
    } else if (result.target_peak_magnitude > 0.0F) {
        const double background_rms = std::sqrt(
            static_cast<double>(background_power / background_count));
        result.snr_db = 20.0 * std::log10(
            static_cast<double>(result.target_peak_magnitude) /
            background_rms);
    }
    result.detected = result.snr_db >= config.minimum_snr_db;
    return result;
}

void test_target_detection_ignores_stronger_alias() {
    radar_sink::TargetConfig config{
        .range_bin = 128,
        .half_width = 2,
        .minimum_snr_db = 10.0,
        .pulses_per_cpi = 4,
    };
    std::vector<uestcradar::ComplexFloat32> bins(512, {1.0F, 0.0F});
    bins[128] = {10.0F, 0.0F};
    bins[20] = {20.0F, 0.0F};

    const auto result = radar_sink::detect_target(bins, config);
    require(result.detected, "in-gate target was not detected");
    require(result.target_peak_bin == 128, "target peak bin is wrong");
    require(result.global_peak_bin == 20, "strong alias was not preserved");

    bins[128] = {1.0F, 0.0F};
    require(!radar_sink::detect_target(bins, config).detected,
            "noise-only gate was accepted");
}

void test_invalid_samples_and_gate() {
    radar_sink::TargetConfig config{.range_bin = 8, .half_width = 1};
    std::vector<uestcradar::ComplexFloat32> bins(32, {1.0F, 0.0F});
    bins[3].i = std::numeric_limits<float>::infinity();
    require_throws(
        [&] { static_cast<void>(radar_sink::detect_target(bins, config)); },
        "Inf sample was accepted");
    bins[3].i = 1.0F;
    config.range_bin = bins.size();
    require_throws(
        [&] { static_cast<void>(radar_sink::detect_target(bins, config)); },
        "out-of-range target gate was accepted");
}

void test_threshold_and_zero_background() {
    radar_sink::TargetConfig config{
        .range_bin = 256,
        .half_width = 0,
        .minimum_snr_db = 10.0,
    };
    std::vector<uestcradar::ComplexFloat32> bins(1024, {1.0F, 0.0F});
    bins[256] = {static_cast<float>(std::sqrt(10.0)), 0.0F};
    const auto boundary = radar_sink::detect_target(bins, config);
    require(std::abs(boundary.snr_db - 10.0) < 1.0e-5,
            "10 dB threshold calculation is wrong");
    require(boundary.detected, "10 dB threshold should be inclusive");

    std::fill(bins.begin(), bins.end(), uestcradar::ComplexFloat32{});
    bins[256] = {1.0F, 0.0F};
    const auto zero_background = radar_sink::detect_target(bins, config);
    require(zero_background.detected &&
                std::isinf(zero_background.snr_db),
            "positive target over zero background was not detected");
}

void test_optimized_detection_matches_reference() {
    radar_sink::TargetConfig config{
        .range_bin = 2048,
        .half_width = 8,
        .minimum_snr_db = 10.0,
        .noise_guard_bins = 64,
    };
    std::vector<uestcradar::ComplexFloat32> bins(4096);
    std::uint32_t state = 0x31415926U;
    for (auto& sample : bins) {
        state = state * 1664525U + 1013904223U;
        sample.i = static_cast<float>(state & 0xffffU) / 4096.0F - 8.0F;
        state = state * 1664525U + 1013904223U;
        sample.q = static_cast<float>(state & 0xffffU) / 4096.0F - 8.0F;
    }
    bins[config.range_bin] = {80.0F, -12.0F};
    bins[17] = {-90.0F, 30.0F};

    const auto expected = reference_detection(bins, config);
    const auto actual = radar_sink::detect_target(bins, config);
    require(actual.detected == expected.detected,
            "optimized detection decision differs from reference");
    require(actual.global_peak_bin == expected.global_peak_bin,
            "optimized global peak differs from reference");
    require(actual.target_peak_bin == expected.target_peak_bin,
            "optimized target peak differs from reference");
    require(std::abs(actual.snr_db - expected.snr_db) < 1.0e-5,
            "optimized SNR differs from reference");

    bins[0] = {std::numeric_limits<float>::max(), 0.0F};
    const auto extreme = radar_sink::detect_target(bins, config);
    require(extreme.global_peak_bin == 0 &&
                std::isfinite(extreme.global_peak_magnitude),
            "large finite sample was not handled safely");
}

void test_parallel_executor_preserves_result_order() {
    radar_sink::TargetConfig config{
        .range_bin = 128,
        .half_width = 2,
        .minimum_snr_db = 10.0,
        .pulses_per_cpi = 4,
    };
    radar_sink::DetectionExecutor executor(4, 8, config);
    std::vector<uestcradar::ComplexFloat32> bins(512, {1.0F, 0.0F});
    bins[128] = {20.0F, 0.0F};
    for (std::uint64_t sequence = 0; sequence < 8; ++sequence) {
        const uestcradar::PulseCompressionMetadata metadata{
            .channel_count = 1,
            .range_bin_count = 512,
            .pulse_index = static_cast<std::uint32_t>(sequence % 4),
            .pulses_per_cpi = 4,
            .range_resolution_m = 1.0,
        };
        executor.submit(sequence, sequence + 1, metadata, 1, 512, bins);
    }
    radar_sink::CpiTracker tracker(config);
    for (std::uint64_t sequence = 0; sequence < 8; ++sequence) {
        const auto result = executor.take(sequence);
        require(result.sequence == sequence,
                "parallel result order was not preserved");
        require(result.detection.detected,
                "parallel detection lost a target");
        const auto observation = tracker.observe(
            result.metadata.pulse_index,
            result.metadata.pulses_per_cpi,
            result.detection);
        if (sequence == 3 || sequence == 7) {
            require(observation.kind == radar_sink::ObservationKind::summary,
                    "ordered results did not complete a CPI");
        }
    }
}

void test_cpi_tracking_and_summary() {
    radar_sink::TargetConfig config{.pulses_per_cpi = 4};
    radar_sink::CpiTracker tracker(config);

    require(
        tracker.observe(2, 4, detected()).kind ==
            radar_sink::ObservationKind::skipped,
        "leading partial CPI was not skipped");
    for (std::uint32_t pulse = 0; pulse < 3; ++pulse) {
        require(
            tracker.observe(pulse, 4, detected()).kind ==
                radar_sink::ObservationKind::accepted,
            "complete CPI pulse was not accepted");
    }
    auto miss = detected(8.0);
    miss.detected = false;
    const auto last = tracker.observe(3, 4, miss);
    require(last.kind == radar_sink::ObservationKind::summary,
            "complete CPI did not produce a summary");
    require(last.summary->pulses == 4 && last.summary->detected == 3,
            "CPI summary counts are wrong");
    require(std::abs(last.summary->minimum_snr_db - 8.0) < 1.0e-9,
            "CPI minimum SNR is wrong");

    require(
        tracker.observe(0, 1, detected()).kind ==
            radar_sink::ObservationKind::skipped,
        "placeholder metadata was not skipped");
}

void test_strict_tracking_failures() {
    radar_sink::TargetConfig config{
        .pulses_per_cpi = 4,
        .fail_on_target_miss = true,
    };
    radar_sink::CpiTracker tracker(config);
    require_throws(
        [&] { static_cast<void>(tracker.observe(0, 1, detected())); },
        "strict metadata mismatch was accepted");

    radar_sink::CpiTracker sequence_tracker(config);
    static_cast<void>(sequence_tracker.observe(0, 4, detected()));
    require_throws(
        [&] {
            static_cast<void>(sequence_tracker.observe(2, 4, detected()));
        },
        "strict pulse discontinuity was accepted");
}

}  // namespace

int main() {
    try {
        test_target_detection_ignores_stronger_alias();
        test_invalid_samples_and_gate();
        test_threshold_and_zero_background();
        test_optimized_detection_matches_reference();
        test_parallel_executor_preserves_result_order();
        test_cpi_tracking_and_summary();
        test_strict_tracking_failures();
        std::cout << "signalsink-test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "signalsink-test: FAIL " << error.what() << '\n';
        return 1;
    }
}
