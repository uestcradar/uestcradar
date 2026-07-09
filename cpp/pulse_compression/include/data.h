#pragma once

#include <common/data_types.h>

#include <cstddef>

namespace cycore::algorithm::pulse_compression {

using InputSample = cy::common::CS16;
using OutputSample = cy::common::CS16;

constexpr std::size_t kDefaultNumChannels = 16;
constexpr std::size_t kDefaultNumPulses = 64;
constexpr std::size_t kDefaultSamplesPerPulse = 512;

} // namespace cycore::algorithm::pulse_compression
