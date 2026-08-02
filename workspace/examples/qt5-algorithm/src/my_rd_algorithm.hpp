#pragma once

#include <data.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace radar_qt_example {

inline constexpr std::uint32_t kPulsesPerCpi = 8;

struct RdResult {
    std::size_t peak_range_bin{};
    std::size_t peak_doppler_bin{};
    float peak_magnitude{};
};

class CpiBuffer {
public:
    void push(
        std::uint32_t pulse_index,
        std::uint32_t pulses_per_cpi,
        std::span<const uestcradar::ComplexFloat32> range_bins) {
        if (pulses_per_cpi != kPulsesPerCpi || range_bins.empty()) {
            throw std::invalid_argument("unexpected CPI shape");
        }
        if (pulse_index == 0) {
            range_bin_count_ = range_bins.size();
            samples_.assign(
                kPulsesPerCpi * range_bin_count_, {});
            received_pulses_ = 0;
        }
        if (range_bin_count_ != range_bins.size() ||
            pulse_index != received_pulses_) {
            throw std::invalid_argument("pulse sequence is not continuous");
        }

        for (std::size_t range = 0; range < range_bins.size(); ++range) {
            samples_[pulse_index * range_bin_count_ + range] =
                range_bins[range];
        }
        ++received_pulses_;
    }

    [[nodiscard]] bool ready() const noexcept {
        return received_pulses_ == kPulsesPerCpi;
    }

    [[nodiscard]] std::size_t range_bin_count() const noexcept {
        return range_bin_count_;
    }

    [[nodiscard]] uestcradar::ComplexFloat32 sample(
        std::size_t pulse,
        std::size_t range) const {
        return samples_.at(pulse * range_bin_count_ + range);
    }

    void clear() noexcept {
        samples_.clear();
        range_bin_count_ = 0;
        received_pulses_ = 0;
    }

private:
    std::vector<uestcradar::ComplexFloat32> samples_;
    std::size_t range_bin_count_{};
    std::uint32_t received_pulses_{};
};

inline RdResult compute_rd(
    const CpiBuffer& cpi,
    uestcradar::Array2D<float> rd) {
    if (!cpi.ready() || rd.rows() != cpi.range_bin_count() ||
        rd.columns() != kPulsesPerCpi) {
        throw std::invalid_argument("RD output shape does not match CPI");
    }

    RdResult result;
    for (std::size_t range = 0; range < rd.rows(); ++range) {
        for (std::size_t doppler = 0;
             doppler < rd.columns();
             ++doppler) {
            std::complex<float> sum;
            for (std::size_t pulse = 0;
                 pulse < kPulsesPerCpi;
                 ++pulse) {
                const auto value = cpi.sample(pulse, range);
                const float angle =
                    -2.0F * std::numbers::pi_v<float> *
                    static_cast<float>(doppler * pulse) /
                    static_cast<float>(kPulsesPerCpi);
                sum += std::complex<float>{value.i, value.q} *
                       std::polar(1.0F, angle);
            }
            const float magnitude = std::abs(sum);
            rd[range][doppler] = magnitude;
            if (magnitude > result.peak_magnitude) {
                result = {range, doppler, magnitude};
            }
        }
    }
    return result;
}

}  // namespace radar_qt_example
