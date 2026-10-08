#pragma once

#include <data.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cfloat>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

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

namespace detail {

struct PowerScan {
    double background_power{};
    std::size_t background_count{};
    double global_peak_power{};
    std::size_t global_peak_bin{};
};

inline double sample_power(const uestcradar::ComplexFloat32& sample) {
    if (!std::isfinite(sample.i) || !std::isfinite(sample.q)) {
        throw std::invalid_argument("pulse frame contains NaN or Inf");
    }
    const double i = sample.i;
    const double q = sample.q;
    return i * i + q * q;
}

inline void update_peak(
    PowerScan& scan, double power, std::size_t index) noexcept {
    if (power > scan.global_peak_power) {
        scan.global_peak_power = power;
        scan.global_peak_bin = index;
    }
}

#if defined(__aarch64__)
inline void scan_neon_range(
    std::span<const uestcradar::ComplexFloat32> bins,
    std::size_t begin,
    std::size_t end,
    bool include_background,
    PowerScan& scan) {
    static_assert(sizeof(uestcradar::ComplexFloat32) == 2 * sizeof(float));
    std::size_t index = begin;
    float64x2_t sum0 = vdupq_n_f64(0.0);
    float64x2_t sum1 = vdupq_n_f64(0.0);
    alignas(16) double powers[4];
    alignas(16) std::uint32_t finite[4];

    for (; index + 4 <= end; index += 4) {
        const auto values = vld2q_f32(
            reinterpret_cast<const float*>(bins.data() + index));
        const auto abs_i = vabsq_f32(values.val[0]);
        const auto abs_q = vabsq_f32(values.val[1]);
        const auto max_float = vdupq_n_f32(FLT_MAX);
        const auto valid = vandq_u32(
            vcleq_f32(abs_i, max_float), vcleq_f32(abs_q, max_float));
        vst1q_u32(finite, valid);
        if (finite[0] != UINT32_MAX || finite[1] != UINT32_MAX ||
            finite[2] != UINT32_MAX || finite[3] != UINT32_MAX) {
            throw std::invalid_argument("pulse frame contains NaN or Inf");
        }

        const auto i0 = vcvt_f64_f32(vget_low_f32(values.val[0]));
        const auto i1 = vcvt_f64_f32(vget_high_f32(values.val[0]));
        const auto q0 = vcvt_f64_f32(vget_low_f32(values.val[1]));
        const auto q1 = vcvt_f64_f32(vget_high_f32(values.val[1]));
        const auto power0 = vfmaq_f64(vmulq_f64(q0, q0), i0, i0);
        const auto power1 = vfmaq_f64(vmulq_f64(q1, q1), i1, i1);
        vst1q_f64(powers, power0);
        vst1q_f64(powers + 2, power1);
        for (std::size_t lane = 0; lane < 4; ++lane) {
            update_peak(scan, powers[lane], index + lane);
        }
        if (include_background) {
            sum0 = vaddq_f64(sum0, power0);
            sum1 = vaddq_f64(sum1, power1);
        }
    }
    if (include_background) {
        scan.background_power += vaddvq_f64(sum0) + vaddvq_f64(sum1);
        scan.background_count += index - begin;
    }
    for (; index < end; ++index) {
        const double power = sample_power(bins[index]);
        update_peak(scan, power, index);
        if (include_background) {
            scan.background_power += power;
            ++scan.background_count;
        }
    }
}
#endif

inline PowerScan scan_powers(
    std::span<const uestcradar::ComplexFloat32> bins,
    std::size_t noise_exclude_begin,
    std::size_t noise_exclude_end) {
    PowerScan scan;
#if defined(__aarch64__)
    scan_neon_range(bins, 0, noise_exclude_begin, true, scan);
    scan_neon_range(
        bins, noise_exclude_begin, noise_exclude_end + 1, false, scan);
    scan_neon_range(
        bins, noise_exclude_end + 1, bins.size(), true, scan);
#else
    for (std::size_t index = 0; index < bins.size(); ++index) {
        const double power = sample_power(bins[index]);
        update_peak(scan, power, index);
        if (index < noise_exclude_begin || index > noise_exclude_end) {
            scan.background_power += power;
            ++scan.background_count;
        }
    }
#endif
    return scan;
}

}  // namespace detail

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

    const auto scan = detail::scan_powers(
        bins, noise_exclude_begin, noise_exclude_end);

    TargetDetection result;
    result.global_peak_bin = scan.global_peak_bin;
    result.global_peak_magnitude = static_cast<float>(
        std::sqrt(scan.global_peak_power));
    result.target_peak_bin = gate_begin;
    double target_peak_power = 0.0;
    for (std::size_t index = gate_begin; index <= gate_end; ++index) {
        const double power = detail::sample_power(bins[index]);
        if (power > target_peak_power) {
            target_peak_power = power;
            result.target_peak_bin = index;
        }
    }
    result.target_peak_magnitude = static_cast<float>(
        std::sqrt(target_peak_power));

    if (scan.background_count == 0 || scan.background_power <= 0.0) {
        result.snr_db = target_peak_power > 0.0
            ? std::numeric_limits<double>::infinity()
            : -std::numeric_limits<double>::infinity();
    } else if (target_peak_power > 0.0) {
        const double mean_background_power =
            scan.background_power / static_cast<double>(scan.background_count);
        result.snr_db = 10.0 * std::log10(
            target_peak_power / mean_background_power);
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
