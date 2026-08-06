#pragma once

#include <data.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace radar_sink {

struct TargetConfig {
    std::size_t range_bin{20480};
    std::size_t half_width{8};
    double minimum_snr_db{10.0};
    std::uint32_t pulses_per_cpi{64};
    std::size_t noise_guard_bins{64};
    bool fail_on_target_miss{false};
};

struct TargetDetection {
    bool detected{};
    std::size_t target_peak_bin{};
    float target_peak_magnitude{};
    std::size_t global_peak_bin{};
    float global_peak_magnitude{};
    double snr_db{-std::numeric_limits<double>::infinity()};
};

inline TargetDetection detect_target(
    std::span<const uestcradar::ComplexFloat32> bins,
    const TargetConfig& config) {
    if (bins.empty() || config.range_bin >= bins.size()) {
        throw std::invalid_argument("target range is outside the pulse frame");
    }
    if (!std::isfinite(config.minimum_snr_db)) {
        throw std::invalid_argument("target SNR threshold is not finite");
    }

    const std::size_t gate_begin = config.range_bin > config.half_width
        ? config.range_bin - config.half_width
        : 0;
    const std::size_t gate_end = std::min(
        bins.size() - 1,
        config.range_bin >
                std::numeric_limits<std::size_t>::max() - config.half_width
            ? bins.size() - 1
            : config.range_bin + config.half_width);
    const std::size_t noise_exclude_begin =
        config.range_bin > config.noise_guard_bins
        ? config.range_bin - config.noise_guard_bins
        : 0;
    const std::size_t noise_exclude_end = std::min(
        bins.size() - 1,
        config.range_bin > std::numeric_limits<std::size_t>::max() -
                config.noise_guard_bins
            ? bins.size() - 1
            : config.range_bin + config.noise_guard_bins);

    TargetDetection result;
    result.target_peak_bin = gate_begin;
    long double background_power = 0.0L;
    std::size_t background_count = 0;
    for (std::size_t index = 0; index < bins.size(); ++index) {
        const float magnitude = std::hypot(bins[index].i, bins[index].q);
        if (!std::isfinite(magnitude)) {
            throw std::invalid_argument("pulse frame contains NaN or Inf");
        }
        if (magnitude > result.global_peak_magnitude) {
            result.global_peak_magnitude = magnitude;
            result.global_peak_bin = index;
        }
        if (index >= gate_begin && index <= gate_end &&
            magnitude > result.target_peak_magnitude) {
            result.target_peak_magnitude = magnitude;
            result.target_peak_bin = index;
        }
        if (index < noise_exclude_begin || index > noise_exclude_end) {
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

enum class ObservationKind {
    skipped,
    accepted,
    summary,
};

struct TargetSummary {
    std::uint64_t cpi_index{};
    std::uint32_t pulses{};
    std::uint32_t detected{};
    double minimum_snr_db{std::numeric_limits<double>::infinity()};
};

struct Observation {
    ObservationKind kind{ObservationKind::skipped};
    std::string reason;
    std::optional<TargetSummary> summary;
};

class CpiTracker {
public:
    explicit CpiTracker(TargetConfig config) : config_(config) {
        if (config_.pulses_per_cpi == 0) {
            throw std::invalid_argument("pulses per CPI must be positive");
        }
    }

    Observation observe(
        std::uint32_t pulse_index,
        std::uint32_t pulses_per_cpi,
        const TargetDetection& detection) {
        if (pulses_per_cpi != config_.pulses_per_cpi) {
            reset_partial();
            if (config_.fail_on_target_miss) {
                throw std::runtime_error(
                    "PulseCompression metadata pulses_per_cpi mismatch");
            }
            return {ObservationKind::skipped,
                    "metadata_pulses_per_cpi_mismatch", std::nullopt};
        }
        if (pulse_index >= config_.pulses_per_cpi) {
            throw std::runtime_error(
                "PulseCompression metadata pulse_index is out of range");
        }

        if (!active_) {
            if (pulse_index != 0) {
                return {ObservationKind::skipped, "leading_partial_cpi",
                        std::nullopt};
            }
            active_ = true;
            next_pulse_index_ = 0;
        }

        if (pulse_index != next_pulse_index_) {
            reset_partial();
            if (config_.fail_on_target_miss) {
                throw std::runtime_error(
                    "PulseCompression pulse_index sequence is discontinuous");
            }
            if (pulse_index != 0) {
                return {ObservationKind::skipped, "discontinuous_cpi",
                        std::nullopt};
            }
            active_ = true;
        }

        ++accepted_pulses_;
        if (detection.detected) {
            ++detected_pulses_;
        }
        minimum_snr_db_ = std::min(minimum_snr_db_, detection.snr_db);
        next_pulse_index_ = pulse_index + 1;

        if (next_pulse_index_ != config_.pulses_per_cpi) {
            return {ObservationKind::accepted, {}, std::nullopt};
        }

        TargetSummary summary{
            .cpi_index = completed_cpis_,
            .pulses = accepted_pulses_,
            .detected = detected_pulses_,
            .minimum_snr_db = minimum_snr_db_,
        };
        ++completed_cpis_;
        reset_partial();
        return {ObservationKind::summary, {}, summary};
    }

    bool has_partial_cpi() const noexcept { return active_; }

private:
    void reset_partial() noexcept {
        active_ = false;
        next_pulse_index_ = 0;
        accepted_pulses_ = 0;
        detected_pulses_ = 0;
        minimum_snr_db_ = std::numeric_limits<double>::infinity();
    }

    TargetConfig config_;
    bool active_{};
    std::uint32_t next_pulse_index_{};
    std::uint32_t accepted_pulses_{};
    std::uint32_t detected_pulses_{};
    std::uint64_t completed_cpis_{};
    double minimum_snr_db_{std::numeric_limits<double>::infinity()};
};

}  // namespace radar_sink
