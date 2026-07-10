#include <range_doppler_algorithm.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

#include <flowgraph/port.h>
#include <flowgraph/value.h>

namespace fg = cy::flowgraph;

using InputSample = cycore::algorithm::range_doppler::InputSample;
using OutputSample = cycore::algorithm::range_doppler::OutputSample;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::int16_t Quantize(double value) {
    const double clamped = std::max<double>(
        static_cast<double>(std::numeric_limits<std::int16_t>::min()),
        std::min<double>(static_cast<double>(std::numeric_limits<std::int16_t>::max()), value));
    return static_cast<std::int16_t>(std::llround(clamped));
}

std::size_t CubeIndex(std::size_t channel_count,
                      std::size_t samples_per_pulse,
                      std::size_t channel,
                      std::size_t pulse,
                      std::size_t sample) {
    return ((pulse * samples_per_pulse + sample) * channel_count) + channel;
}

InputSample MakeTone(std::size_t pulses,
                     std::size_t samples_per_pulse,
                     std::size_t pulse,
                     std::size_t sample,
                     double range_bin,
                     double doppler_bin,
                     double amplitude) {
    const double phase = 2.0 * kPi *
                         (range_bin * static_cast<double>(sample) / static_cast<double>(samples_per_pulse) +
                          doppler_bin * static_cast<double>(pulse) / static_cast<double>(pulses));
    return InputSample{Quantize(amplitude * std::cos(phase)),
                       Quantize(amplitude * std::sin(phase))};
}

} // namespace

int main() {
    const std::size_t channel_count = 3;
    const std::size_t pulses = 4;
    const std::size_t samples_per_pulse = 8;
    const std::size_t range_bin = 2;
    const std::size_t doppler_bin = 1;
    const std::size_t element_count = channel_count * pulses * samples_per_pulse;

    fg::ValueMap params;
    params["num_channels"] = static_cast<std::int64_t>(channel_count);
    params["num_pulses"] = static_cast<std::int64_t>(pulses);
    params["samples_per_pulse"] = static_cast<std::int64_t>(samples_per_pulse);

    fg::PortOut<InputSample> source;
    cycore::sdk::AlgorithmBlockAdapter<RangeDopplerAlgorithm, InputSample, OutputSample> block(params);
    fg::PortIn<OutputSample> sink;
    fg::connect(source, block.in, element_count * 2);
    fg::connect(block.out, sink, element_count * 2);

    auto input = source.reserve(element_count);
    assert(input.size() == element_count);
    for (std::size_t pulse = 0; pulse < pulses; ++pulse) {
        for (std::size_t sample = 0; sample < samples_per_pulse; ++sample) {
            for (std::size_t channel = 0; channel < channel_count; ++channel) {
                input[CubeIndex(channel_count, samples_per_pulse, channel, pulse, sample)] =
                    MakeTone(pulses, samples_per_pulse, pulse, sample, range_bin, doppler_bin, 1000.0);
            }
        }
    }
    input.commit(element_count);

    block.work();
    auto output = sink.get(element_count);
    // 🟢 算子实际输出通道数为 1，总元素数应为 1 * pulses * samples_per_pulse = 32
    assert(output.size() == pulses * samples_per_pulse);
    
    const auto peak = output[CubeIndex(1, samples_per_pulse, 0, doppler_bin, range_bin)];
    assert(peak > 0.0f);
    assert(output[CubeIndex(1, samples_per_pulse, 0, 0, 0)] == 0.0f);
    
    output.consume(pulses * samples_per_pulse);

    std::cout << "Range-Doppler block test passed." << std::endl;
    return 0;
}
