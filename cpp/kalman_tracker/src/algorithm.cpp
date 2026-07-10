#include "data.h"
#include "kalman_tracker_algorithm.h"

CYCORE_EXPORT_ALGORITHM(
    "kalman_tracker",
    "algorithm.kalman_tracker",
    KalmanTrackerAlgorithm,
    cycore::algorithm::kalman_tracker::InputSample,
    cycore::algorithm::kalman_tracker::OutputSample
)
