#include <cfar_plotter_algorithm.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <flowgraph/port.h>
#include <flowgraph/value.h>

namespace fg = cy::flowgraph;

using InputSample = cycore::algorithm::cfar_plotter::InputSample;
using OutputSample = cycore::algorithm::cfar_plotter::OutputSample;

std::size_t CubeIndex(std::size_t channel_count,
                      std::size_t samples_per_pulse,
                      std::size_t channel,
                      std::size_t pulse,
                      std::size_t sample) {
    return ((pulse * samples_per_pulse + sample) * channel_count) + channel;
}

int main() {
    const std::size_t channel_count = 3;
    const std::size_t pulses = 4;
    const std::size_t samples_per_pulse = 8;
    const std::size_t element_count = channel_count * pulses * samples_per_pulse;

    fg::ValueMap params;
    params["num_channels"] = static_cast<std::int64_t>(channel_count);
    params["num_pulses"] = static_cast<std::int64_t>(pulses);
    params["samples_per_pulse"] = static_cast<std::int64_t>(samples_per_pulse);
    params["threshold"] = 10.0;

    fg::PortOut<InputSample> source;
    cycore::sdk::AlgorithmBlockAdapter<CfarPlotterAlgorithm, InputSample, OutputSample> block(params);
    fg::PortIn<OutputSample> sink;
    fg::connect(source, block.in, element_count * 2);
    fg::connect(block.out, sink, 1024);

    auto input = source.reserve(element_count);
    assert(input.size() == element_count);
    for (std::size_t i = 0; i < element_count; ++i) {
        input[i] = 0.0f;
    }
    input[CubeIndex(channel_count, samples_per_pulse, 0, 1, 2)] = 42.0f;
    input[CubeIndex(channel_count, samples_per_pulse, 2, 3, 4)] = 84.0f;
    input.commit(element_count);

    block.work();
    cycore::sdk::Reader<std::byte> reader(sink);
    auto plots = reader.read_raw_array<cycore::algorithm::cfar_plotter::Plot>();
    assert(plots);
    assert(plots->size() == 2);
    assert((*plots)[0].channel == 0);
    assert((*plots)[0].doppler_bin == 1);
    assert((*plots)[0].range_bin == 2);
    assert((*plots)[1].channel == 2);
    assert((*plots)[1].doppler_bin == 3);
    assert((*plots)[1].range_bin == 4);
    reader.consume();

    std::cout << "CFAR plotter block test passed." << std::endl;
    return 0;
}
