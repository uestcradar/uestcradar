#pragma once

#include "../rd_contract.hpp"
#include "sha256.hpp"

#include <data.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace radar_qt_example {

struct RdVerification {
    std::string digest;
    std::size_t peak_range_bin{};
    std::size_t peak_doppler_bin{};
    float peak_magnitude{};
};

inline RdVerification verify_rd_frame(
    const uestcradar::RDMetadata& metadata,
    std::span<const float> samples,
    std::size_t rows,
    std::size_t columns) {
    if (metadata.channel_index != 0 ||
        metadata.range_bin_count == 0 ||
        metadata.range_bin_count > kMaxRangeBinCount ||
        metadata.doppler_bin_count != kDopplerBinCount ||
        !std::isfinite(metadata.range_resolution_m) ||
        metadata.range_resolution_m <= 0.0 ||
        !std::isfinite(metadata.velocity_resolution_mps) ||
        metadata.velocity_resolution_mps <= 0.0) {
        throw std::invalid_argument("RDFrame metadata is invalid");
    }
    const auto expected_samples = static_cast<std::size_t>(
        metadata.range_bin_count) * metadata.doppler_bin_count;
    if (rows != metadata.range_bin_count ||
        columns != metadata.doppler_bin_count ||
        samples.size() != expected_samples ||
        kRdMetadataBytes + samples.size_bytes() !=
            rd_frame_bytes(metadata.range_bin_count) ||
        rd_frame_bytes(metadata.range_bin_count) > kMaxFrameBytes) {
        throw std::invalid_argument("RDFrame payload shape or length is invalid");
    }

    RdVerification result;
    result.peak_magnitude = std::numeric_limits<float>::lowest();
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const float sample = samples[index];
        if (!std::isfinite(sample)) {
            throw std::invalid_argument("RDFrame contains a non-finite value");
        }
        if (sample > result.peak_magnitude) {
            result.peak_magnitude = sample;
            result.peak_range_bin = index / columns;
            result.peak_doppler_bin = index % columns;
        }
    }
    result.digest = sha256(std::as_bytes(samples));
    return result;
}

}  // namespace radar_qt_example
