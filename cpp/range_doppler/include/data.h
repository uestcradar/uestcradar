#pragma once

#include <common/data_types.h>
#include <cstddef>

namespace cycore::algorithm::range_doppler {

using InputSample = cy::common::CS16;
using OutputSample = float;

// 默认维度设定（主要作为备用，实际计算时必须从配置中动态获取）
constexpr std::size_t kDefaultNumChannels = 8;
constexpr std::size_t kDefaultNumPulses = 64;
constexpr std::size_t kDefaultSamplesPerPulse = 512;

} // namespace cycore::algorithm::range_doppler
