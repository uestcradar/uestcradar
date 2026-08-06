#include "target_validator.hpp"

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
        test_cpi_tracking_and_summary();
        test_strict_tracking_failures();
        std::cout << "signalsink-test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "signalsink-test: FAIL " << error.what() << '\n';
        return 1;
    }
}
