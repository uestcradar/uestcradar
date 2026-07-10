#pragma once

#include <common/data_types.h>
#include <cstddef>

namespace cycore::algorithm::fft_block {

// 🟢 错误纠正 1: 直接复用项目全局标准复数类型 cy::common::CS16。
// 严禁自创 ComplexInt16 结构体，这会导致 C++ 强类型 Port 连接失效。
using InputSample = cy::common::CS16;
using OutputSample = cy::common::CS16;

// 🟢 错误纠正 2: 严禁将维度写死为 1024，必须使用 Default 前缀提供“默认值”。
// 真正的维度大小应该在算法构造函数中从 Params 动态读取，实现算法的动态尺寸适配。
constexpr std::size_t kDefaultNumChannels = 16;
constexpr std::size_t kDefaultSamplesPerPulse = 1024; // 1024 点 FFT

} // namespace cycore::algorithm::fft_block
