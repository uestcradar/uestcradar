#pragma once

#include "../rd_contract.hpp"
#include "sha256.hpp"

#include <data.h>

#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>

namespace radar_qt_example {

inline std::string verify_rd_frame(
    const uestcradar::RDMetadata& metadata,
    std::span<const float> samples,
    std::size_t rows,
    std::size_t columns) {
    if (metadata.channel_index != 0 ||
        metadata.range_bin_count != kRangeBinCount ||
        metadata.doppler_bin_count != kDopplerBinCount ||
        metadata.range_resolution_m != kRangeResolutionM ||
        metadata.velocity_resolution_mps != kVelocityResolutionMps) {
        throw std::invalid_argument("RDFrame metadata is invalid");
    }
    if (rows != kRangeBinCount || columns != kDopplerBinCount ||
        samples.size() != kRdSampleCount ||
        kRdMetadataBytes + samples.size_bytes() != kRdFrameBytes) {
        throw std::invalid_argument("RDFrame payload shape or length is invalid");
    }
    for (const auto sample : samples) {
        if (!std::isfinite(sample)) {
            throw std::invalid_argument("RDFrame contains a non-finite value");
        }
    }
    return sha256(std::as_bytes(samples));
}

}  // namespace radar_qt_example
