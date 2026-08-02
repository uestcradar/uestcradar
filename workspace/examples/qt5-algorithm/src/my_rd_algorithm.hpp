#pragma once

#include "rd_contract.hpp"

#include <data.h>

#include <QVector>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace radar_qt_example {

class CpiBuffer {
public:
    explicit CpiBuffer(std::uint32_t range_bin_count = kRangeBinCount)
        : range_bin_count_(range_bin_count) {
        if (range_bin_count_ == 0 ||
            range_bin_count_ > static_cast<std::size_t>(INT_MAX) /
                kPulsesPerCpi) {
            throw std::invalid_argument("CPI range-bin count is invalid");
        }
    }

    void push(
        const uestcradar::PulseCompressionMetadata& metadata,
        std::span<const uestcradar::ComplexFloat32> range_bins) {
        if (metadata.channel_count != kChannelCount ||
            metadata.range_bin_count != range_bin_count_ ||
            metadata.pulses_per_cpi != kPulsesPerCpi ||
            metadata.range_resolution_m != kRangeResolutionM ||
            range_bins.size() != range_bin_count_) {
            throw std::invalid_argument(
                "PulseCompressionFrame does not match the developer-base contract");
        }
        if (metadata.pulse_index != received_pulses_) {
            throw std::invalid_argument(
                "pulse sequence is missing, duplicated, or out of order");
        }
        if (received_pulses_ == 0) {
            samples_.resize(static_cast<int>(
                range_bin_count_ * kPulsesPerCpi));
        }

        const auto destination = samples_.begin() +
            static_cast<std::ptrdiff_t>(
                metadata.pulse_index * range_bin_count_);
        std::copy(range_bins.begin(), range_bins.end(), destination);
        ++received_pulses_;
    }

    [[nodiscard]] bool ready() const noexcept {
        return received_pulses_ == kPulsesPerCpi;
    }

    [[nodiscard]] std::uint32_t range_bin_count() const noexcept {
        return range_bin_count_;
    }

    [[nodiscard]] const uestcradar::ComplexFloat32& sample(
        std::size_t pulse,
        std::size_t range) const {
        if (!ready() || pulse >= kPulsesPerCpi ||
            range >= range_bin_count_) {
            throw std::out_of_range("CPI sample is unavailable");
        }
        return samples_.at(static_cast<int>(
            pulse * range_bin_count_ + range));
    }

    void clear() {
        samples_.clear();
        received_pulses_ = 0;
    }

private:
    QVector<uestcradar::ComplexFloat32> samples_;
    std::uint32_t range_bin_count_;
    std::uint32_t received_pulses_{};
};

struct RdResult {
    std::size_t peak_range_bin{};
    std::size_t peak_doppler_bin{};
    float peak_magnitude{};
};

// This is intentionally a fast placeholder, not an FFT/DFT implementation.
// Replace the function body with the real Qt5 Range-Doppler algorithm.
inline RdResult compute_rd(
    const CpiBuffer& pulse_cpi,
    uestcradar::Array2D<float> rd_map) {
    if (!pulse_cpi.ready() ||
        rd_map.rows() != pulse_cpi.range_bin_count() ||
        rd_map.columns() != kDopplerBinCount) {
        throw std::invalid_argument("RD output shape does not match the CPI");
    }

    RdResult result;
    for (std::size_t range = 0; range < rd_map.rows(); ++range) {
        for (std::size_t doppler = 0;
             doppler < rd_map.columns();
             ++doppler) {
            const auto& sample = pulse_cpi.sample(
                doppler % kPulsesPerCpi, range);
            const auto magnitude = std::hypot(sample.i, sample.q);
            rd_map[range][doppler] = magnitude;
            if (magnitude > result.peak_magnitude) {
                result = {range, doppler, magnitude};
            }
        }
    }
    return result;
}

}  // namespace radar_qt_example
