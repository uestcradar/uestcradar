#include <pulse_compression_algorithm.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <flowgraph/port.h>
#include <flowgraph/value.h>

namespace fg = cy::flowgraph;

using InputSample = cycore::algorithm::pulse_compression::InputSample;
using OutputSample = cycore::algorithm::pulse_compression::OutputSample;

std::size_t CubeIndex(std::size_t channel_count,
                      std::size_t samples_per_pulse,
                      std::size_t channel,
                      std::size_t pulse,
                      std::size_t sample) {
    return ((pulse * samples_per_pulse + sample) * channel_count) + channel;
}

int main() {
    const std::size_t channel_count = 3;
    const std::size_t pulses = 2;
    const std::size_t samples_per_pulse = 4;
    const std::size_t element_count = channel_count * pulses * samples_per_pulse;

    fg::ValueMap params;
    params["num_channels"] = static_cast<std::int64_t>(channel_count);
    params["num_pulses"] = static_cast<std::int64_t>(pulses);
    params["samples_per_pulse"] = static_cast<std::int64_t>(samples_per_pulse);

    fg::PortOut<InputSample> source;
    cycore::sdk::AlgorithmBlockAdapter<PulseCompressionAlgorithm, InputSample, OutputSample> block(params);
    fg::PortIn<OutputSample> sink;
    fg::connect(source, block.in, element_count * 2);
    fg::connect(block.out, sink, element_count * 2);

    auto input = source.reserve(element_count);
    assert(input.size() == element_count);
    for (std::size_t pulse = 0; pulse < pulses; ++pulse) {
        for (std::size_t sample = 0; sample < samples_per_pulse; ++sample) {
            for (std::size_t channel = 0; channel < channel_count; ++channel) {
                const auto index = CubeIndex(channel_count, samples_per_pulse, channel, pulse, sample);
                input[index] = InputSample{
                    static_cast<std::int16_t>((100 * pulse + 10 * sample + channel) * 256),
                    static_cast<std::int16_t>(-(100 * static_cast<int>(pulse) + 10 * static_cast<int>(sample) + static_cast<int>(channel)) * 256)};
            }
        }
    }
    input.commit(element_count);

    block.work();
    auto output = sink.get(element_count);
    assert(output.size() == element_count);
    for (std::size_t i = 0; i < element_count; ++i) {
        assert(output[i].i == input[i].i / 256);
        assert(output[i].q == input[i].q / 256);
    }
    output.consume(element_count);

    std::cout << "Pulse compression block test passed." << std::endl;
    return 0;
}
