#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cycore::algorithm::kalman_tracker {

using InputSample = std::byte;
using OutputSample = std::byte;

struct Plot {
    std::uint32_t channel;
    std::uint32_t doppler_bin;
    std::uint32_t range_bin;
    float power;
    float range;
    float velocity;
};

struct Track {
    std::uint32_t id;
    std::uint32_t age;
    float range;
    float velocity;
    float power;
};

static_assert(std::is_standard_layout<Plot>::value, "Plot must be standard-layout");
static_assert(std::is_trivially_copyable<Plot>::value, "Plot must be trivially copyable");
static_assert(alignof(Plot) <= 8, "Plot must fit the RawBytes frame alignment contract");

static_assert(std::is_standard_layout<Track>::value, "Track must be standard-layout");
static_assert(std::is_trivially_copyable<Track>::value, "Track must be trivially copyable");
static_assert(alignof(Track) <= 8, "Track must fit the RawBytes frame alignment contract");

} // namespace cycore::algorithm::kalman_tracker
