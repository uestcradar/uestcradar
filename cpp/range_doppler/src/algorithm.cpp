#include "range_doppler_algorithm.h"

#include <cycore_algorithm_sdk.h>

// 导出与 node0 的 YAML 对应的注册信息
CYCORE_EXPORT_ALGORITHM(
    "range_doppler",
    "algorithm.range_doppler",
    RangeDopplerAlgorithm,
    cycore::algorithm::range_doppler::InputSample,
    cycore::algorithm::range_doppler::OutputSample
)
