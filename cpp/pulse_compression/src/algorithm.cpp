#include "data.h"
#include "pulse_compression_algorithm.h"

CYCORE_EXPORT_ALGORITHM(
    "pulse_compression",
    "algorithm.pulse_compression",
    PulseCompressionAlgorithm,
    cycore::algorithm::pulse_compression::InputSample,
    cycore::algorithm::pulse_compression::OutputSample
)
