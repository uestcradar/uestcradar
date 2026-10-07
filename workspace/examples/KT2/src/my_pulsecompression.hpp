#pragma once

#include <data.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace radar_example {

inline constexpr std::size_t kMaxFramePayloadBytes = 32U * 1024U * 1024U;
inline constexpr std::size_t kPulseMetadataBytes = 24;
inline constexpr double kSpeedOfLightMps = 299792458.0;

inline std::size_t pulse_payload_bytes(
    std::uint32_t channels, std::uint32_t range_bins) {
    constexpr auto element_bytes = sizeof(uestcradar::ComplexFloat32);
    const auto elements = static_cast<std::uint64_t>(channels) * range_bins;
    if (elements >
        (kMaxFramePayloadBytes - kPulseMetadataBytes) / element_bytes) {
        throw std::invalid_argument(
            "one output frame exceeds the 32 MiB payload limit");
    }
    return kPulseMetadataBytes +
        static_cast<std::size_t>(elements) * element_bytes;
}

inline uestcradar::PulseCompressionMetadata describe_output(
    const uestcradar::IQMetadata& input) {
    if (input.channel_count == 0 || input.samples_per_channel == 0 ||
        !std::isfinite(input.sample_rate_hz) || input.sample_rate_hz <= 0.0) {
        throw std::invalid_argument("IQ metadata cannot describe an output frame");
    }
    static_cast<void>(pulse_payload_bytes(
        input.channel_count, input.samples_per_channel));
    return {
        .channel_count = input.channel_count,
        .range_bin_count = input.samples_per_channel,
        .pulse_index = 0,
        .pulses_per_cpi = 1,
        .range_resolution_m =
            kSpeedOfLightMps / (2.0 * input.sample_rate_hz),
    };
}

inline void dequantize_samples(
    uestcradar::Array2D<const uestcradar::ComplexInt16> input,
    uestcradar::Array2D<uestcradar::ComplexFloat32> output,
    double scale) {
    if (input.rows() != output.rows() ||
        input.columns() != output.columns() || !std::isfinite(scale)) {
        throw std::invalid_argument("input and output matrix shapes are invalid");
    }
    for (std::size_t row = 0; row < input.rows(); ++row) {
        for (std::size_t column = 0; column < input.columns(); ++column) {
            output[row][column] = {
                static_cast<float>(input[row][column].i * scale),
                static_cast<float>(input[row][column].q * scale),
            };
        }
    }
}

inline void dequantize(
    const uestcradar::IQFrame& input,
    uestcradar::PulseCompressionFrame& output) {
    dequantize_samples(input.data(), output.data(),
                       input.metadata().dequantization_scale);
}

}  // namespace radar_example
