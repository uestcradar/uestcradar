#include "data.h"
#include "cfar_plotter_algorithm.h"

CYCORE_EXPORT_ALGORITHM(
    "cfar_plotter",
    "algorithm.cfar_plotter",
    CfarPlotterAlgorithm,
    cycore::algorithm::cfar_plotter::InputSample,
    cycore::algorithm::cfar_plotter::OutputSample
)
