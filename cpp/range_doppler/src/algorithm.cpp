#include "data.h"
#include "range_doppler_algorithm.h"

CYCORE_EXPORT_ALGORITHM(
    "range_doppler",
    "algorithm.range_doppler",
    RangeDopplerAlgorithm,
    cycore::algorithm::range_doppler::InputSample,
    cycore::algorithm::range_doppler::OutputSample
)
