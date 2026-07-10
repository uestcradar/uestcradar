#pragma once

#include <cstddef>

namespace cycore::algorithm::my_block {

using InputSample = float;
using OutputSample = float;

constexpr std::size_t kInputRows = 1;
constexpr std::size_t kInputCols = 1024;
constexpr std::size_t kOutputRows = 1;
constexpr std::size_t kOutputCols = 1024;

} // namespace cycore::algorithm::my_block
