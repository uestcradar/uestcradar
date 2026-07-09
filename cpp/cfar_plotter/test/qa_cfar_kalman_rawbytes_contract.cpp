#include "../cfar_plotter/include/data.h"
#include "../kalman_tracker/include/data.h"

#include <cassert>
#include <cstddef>
#include <iostream>

int main() {
    using UpstreamPlot = cycore::algorithm::cfar_plotter::Plot;
    using DownstreamPlot = cycore::algorithm::kalman_tracker::Plot;

    static_assert(sizeof(UpstreamPlot) == sizeof(DownstreamPlot),
                  "CFAR and tracker Plot ABI sizes must match");
    static_assert(alignof(UpstreamPlot) == alignof(DownstreamPlot),
                  "CFAR and tracker Plot ABI alignments must match");

    assert(offsetof(UpstreamPlot, channel) == offsetof(DownstreamPlot, channel));
    assert(offsetof(UpstreamPlot, doppler_bin) == offsetof(DownstreamPlot, doppler_bin));
    assert(offsetof(UpstreamPlot, range_bin) == offsetof(DownstreamPlot, range_bin));
    assert(offsetof(UpstreamPlot, power) == offsetof(DownstreamPlot, power));
    assert(offsetof(UpstreamPlot, range) == offsetof(DownstreamPlot, range));
    assert(offsetof(UpstreamPlot, velocity) == offsetof(DownstreamPlot, velocity));

    std::cout << "CFAR/Kalman RawBytes ABI contract test passed." << std::endl;
    return 0;
}
