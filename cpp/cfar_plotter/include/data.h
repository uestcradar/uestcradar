#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cycore::algorithm::cfar_plotter {

using InputSample = float;
using OutputSample = std::byte;

constexpr std::size_t kDefaultNumChannels = 16;
constexpr std::size_t kDefaultNumPulses = 64;
constexpr std::size_t kDefaultSamplesPerPulse = 512;
constexpr float kDefaultThreshold = 1.0f;

struct Plot {
    std::uint32_t channel;
    std::uint32_t doppler_bin;
    std::uint32_t range_bin;
    float power;
    float range;
    float velocity;
};

static_assert(std::is_standard_layout<Plot>::value, "Plot must be standard-layout");
static_assert(std::is_trivially_copyable<Plot>::value, "Plot must be trivially copyable");
static_assert(alignof(Plot) <= 8, "Plot must fit the RawBytes frame alignment contract");

} // namespace cycore::algorithm::cfar_plotter
