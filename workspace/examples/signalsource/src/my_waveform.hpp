#pragma once

#include <data.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace radar_example {

inline constexpr std::uint32_t kRangeBins = 64;
inline constexpr std::uint32_t kPulsesPerCpi = 8;
inline constexpr std::uint32_t kTargetRangeBin = 17;
inline constexpr std::uint32_t kTargetDopplerBin = 2;

inline uestcradar::IQMetadata iq_metadata(
    std::uint32_t channels,
    std::uint32_t samples_per_channel) {
    return {
        .cpi_index = 0,
        .channel_count = channels,
        .samples_per_channel = samples_per_channel,
        .pulse_count = 1,
        .wave_process_type = 0,
        .velocity_oversampling = 1,
        .sample_rate_hz = 1.0e6,
        .nominal_carrier_frequency_hz = 10.0e9,
        .bandwidth_hz = 1.0e6,
        .pulse_width_s = 1.0e-6,
        .nominal_prt_s = 1.0e-3,
        .observation_max_range_m = 1.0e3,
        .dequantization_scale = 1.0,
    };
}

inline uestcradar::ComplexInt16 iq_sample(
    std::uint32_t channel,
    std::size_t index,
    std::size_t samples_per_channel) {
    const std::size_t period = 192 + static_cast<std::size_t>(channel) * 64;
    const std::int32_t phase = static_cast<std::int32_t>(
        (index * (channel + 1U)) % period);
    const std::int32_t centered = phase - static_cast<std::int32_t>(period / 2);
    const std::int32_t baseline = centered *
        static_cast<std::int32_t>(18'000 / (period / 2));
    const std::size_t pulse_center = samples_per_channel *
        (static_cast<std::size_t>(channel) + 1) / 5;
    const bool pulse = index >= pulse_center && index < pulse_center + 8;
    const std::int16_t peak = static_cast<std::int16_t>(
        std::min<std::uint32_t>(30'000, 18'000 + channel * 3'000));
    return {
        pulse ? peak : static_cast<std::int16_t>(baseline),
        pulse ? static_cast<std::int16_t>(-peak)
              : static_cast<std::int16_t>(-baseline / 2),
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
    auto matrix = frame.data();
    for (std::size_t channel = 0; channel < matrix.rows(); ++channel) {
        auto samples = matrix[channel];
        for (std::size_t index = 0; index < samples.size(); ++index) {
            samples[index] = iq_sample(
                static_cast<std::uint32_t>(channel), index, samples.size());
        }
    }
}

inline std::vector<uestcradar::ComplexInt16> make_iq_waveform(
    std::uint32_t channels,
    std::uint32_t samples_per_channel) {
    std::vector<uestcradar::ComplexInt16> values(
        static_cast<std::size_t>(channels) * samples_per_channel);
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        for (std::uint32_t index = 0;
             index < samples_per_channel;
             ++index) {
            values[static_cast<std::size_t>(channel) *
                       samples_per_channel + index] =
                iq_sample(channel, index, samples_per_channel);
        }
    }
    return values;
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
