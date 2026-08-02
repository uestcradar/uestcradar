#pragma once

#include <data.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace radar_example {

inline constexpr std::uint32_t kIqSamples = 128;
inline constexpr std::uint32_t kRangeBins = 64;
inline constexpr std::uint32_t kPulsesPerCpi = 8;
inline constexpr std::uint32_t kTargetRangeBin = 17;
inline constexpr std::uint32_t kTargetDopplerBin = 2;

inline uestcradar::IQMetadata iq_metadata() {
    return {
        .channel_count = 1,
        .samples_per_channel = kIqSamples,
        .sample_rate_hz = 1.0e6,
        .center_frequency_hz = 10.0e9,
    };
}

inline uestcradar::PulseCompressionMetadata pulse_metadata(
    std::uint64_t sequence) {
    return {
        .channel_count = 1,
        .range_bin_count = kRangeBins,
        .pulse_index = static_cast<std::uint32_t>(
            (sequence - 1) % kPulsesPerCpi),
        .pulses_per_cpi = kPulsesPerCpi,
        .range_resolution_m = 1.5,
    };
}

inline void fill_iq(uestcradar::IQFrame& frame) {
    auto samples = frame.data()[0];
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const double position =
            static_cast<double>(index) / static_cast<double>(samples.size());
        const double phase = std::numbers::pi * 16.0 * position * position;
        samples[index] = {
            static_cast<std::int16_t>(std::cos(phase) * 20'000.0),
            static_cast<std::int16_t>(std::sin(phase) * 20'000.0),
        };
    }
}

inline void fill_pulse(
    uestcradar::PulseCompressionFrame& frame) {
    auto bins = frame.data()[0];
    for (auto& value : bins) {
        value = {};
    }

    const auto pulse_index = frame.metadata().pulse_index;
    const double phase =
        2.0 * std::numbers::pi *
        static_cast<double>(kTargetDopplerBin * pulse_index) /
        static_cast<double>(kPulsesPerCpi);
    bins[kTargetRangeBin] = {
        static_cast<float>(100.0 * std::cos(phase)),
        static_cast<float>(100.0 * std::sin(phase)),
    };
}

}  // namespace radar_example
