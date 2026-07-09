#pragma once

#include <common/data_types.h>

#include <cstddef>

namespace cycore::algorithm::range_doppler {

using InputSample = cy::common::CS16;
using OutputSample = float;

constexpr std::size_t kDefaultNumChannels = 16;
constexpr std::size_t kDefaultNumPulses = 64;
constexpr std::size_t kDefaultSamplesPerPulse = 512;

} // namespace cycore::algorithm::range_doppler
