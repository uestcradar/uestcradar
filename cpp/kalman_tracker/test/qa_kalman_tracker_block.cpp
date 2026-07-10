#include <kalman_tracker_algorithm.h>

#include <cassert>
#include <cstddef>
#include <iostream>

#include <flowgraph/port.h>
#include <flowgraph/value.h>

namespace fg = cy::flowgraph;

using InputSample = cycore::algorithm::kalman_tracker::InputSample;
using OutputSample = cycore::algorithm::kalman_tracker::OutputSample;

int main() {
    fg::ValueMap params;
    fg::PortOut<InputSample> source;
    cycore::sdk::AlgorithmBlockAdapter<KalmanTrackerAlgorithm, InputSample, OutputSample> block(params);
    fg::PortIn<OutputSample> sink;
    fg::connect(source, block.in, 1024);
    fg::connect(block.out, sink, 1024);

    cycore::sdk::Writer<std::byte> writer(source);
    auto plots = writer.reserve_raw_array<cycore::algorithm::kalman_tracker::Plot>(2);
    assert(plots);
    (*plots)[0] = cycore::algorithm::kalman_tracker::Plot{0, 1, 2, 42.0f, 2.0f, 1.0f};
    (*plots)[1] = cycore::algorithm::kalman_tracker::Plot{2, 3, 4, 84.0f, 4.0f, 3.0f};
    writer.commit();

    block.work();
    cycore::sdk::Reader<std::byte> reader(sink);
    auto tracks = reader.read_raw_array<cycore::algorithm::kalman_tracker::Track>();
    assert(tracks);
    assert(tracks->size() == 2);
    assert((*tracks)[0].id == 1);
    assert((*tracks)[0].range == 2.0f);
    assert((*tracks)[0].velocity == 1.0f);
    assert((*tracks)[1].id == 2);
    assert((*tracks)[1].range == 4.0f);
    assert((*tracks)[1].velocity == 3.0f);
    reader.consume();

    std::cout << "Kalman tracker block test passed." << std::endl;
    return 0;
}
