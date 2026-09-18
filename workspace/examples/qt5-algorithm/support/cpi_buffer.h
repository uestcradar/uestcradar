#pragma once

#include "rd_contract.hpp"

#include <data.h>

#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <vector>

namespace radar_qt_example {

class CpiBuffer {
public:
    void push(
        const uestcradar::PulseCompressionMetadata& metadata,
        std::span<const uestcradar::ComplexFloat32> range_bins) {
        validate_frame(metadata, range_bins);

        if (metadata.pulse_index == 0 && received_pulses_ != 0) {
            clear();
        }
        if (received_pulses_ == 0) {
            if (metadata.pulse_index != 0) {
                return;
            }
            begin_cpi(metadata);
        } else if (!matches_current_cpi(metadata) ||
                   metadata.pulse_index != received_pulses_) {
            clear();
            return;
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

    [[nodiscard]] double range_resolution_m() const noexcept {
        return range_resolution_m_;
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
        range_bin_count_ = 0;
        range_resolution_m_ = 0.0;
        received_pulses_ = 0;
    }

private:
    static void validate_frame(
        const uestcradar::PulseCompressionMetadata& metadata,
        std::span<const uestcradar::ComplexFloat32> range_bins) {
        if (metadata.channel_count != kChannelCount ||
            metadata.pulses_per_cpi != kPulsesPerCpi ||
            metadata.pulse_index >= kPulsesPerCpi ||
            metadata.range_bin_count == 0 ||
            metadata.range_bin_count > kMaxInputRangeBinCount ||
            !std::isfinite(metadata.range_resolution_m) ||
            metadata.range_resolution_m <= 0.0 ||
            range_bins.size() != metadata.range_bin_count) {
            throw std::invalid_argument(
                "PulseCompressionFrame does not match the developer-base contract");
        }
    }

    void begin_cpi(const uestcradar::PulseCompressionMetadata& metadata) {
        range_bin_count_ = metadata.range_bin_count;
        range_resolution_m_ = metadata.range_resolution_m;
        samples_.resize(static_cast<int>(
            static_cast<std::size_t>(range_bin_count_) * kPulsesPerCpi));
    }

    [[nodiscard]] bool matches_current_cpi(
        const uestcradar::PulseCompressionMetadata& metadata) const noexcept {
        return metadata.range_bin_count == range_bin_count_ &&
            metadata.range_resolution_m == range_resolution_m_;
    }

    QVector<uestcradar::ComplexFloat32> samples_;
    std::uint32_t range_bin_count_{};
    double range_resolution_m_{};
    std::uint32_t received_pulses_{};
};

} // namespace radar_qt_example
