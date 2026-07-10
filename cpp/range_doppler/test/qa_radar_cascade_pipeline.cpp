#include <pulse_compression_algorithm.h>
#include <range_doppler_algorithm.h>
#include <cfar_plotter_algorithm.h>
#include <kalman_tracker_algorithm.h>

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

cy::common::CS16 MakeTone(std::size_t pulses,
                          std::size_t samples_per_pulse,
                          std::size_t pulse,
                          std::size_t sample,
                          double range_bin,
                          double doppler_bin,
                          double amplitude) {
    const double phase = 2.0 * kPi *
                         (range_bin * static_cast<double>(sample) / static_cast<double>(samples_per_pulse) +
                          doppler_bin * static_cast<double>(pulse) / static_cast<double>(pulses));
    return cy::common::CS16{Quantize(amplitude * std::cos(phase)),
                            Quantize(amplitude * std::sin(phase))};
}

} // namespace

int main() {
    const std::size_t channel_count = 5;
    const std::size_t pulses = 4;
    const std::size_t samples_per_pulse = 8;
    const std::size_t range_bin = 2;
    const std::size_t doppler_bin = 1;
    const std::size_t element_count = channel_count * pulses * samples_per_pulse;

    fg::ValueMap params;
    params["num_channels"] = static_cast<std::int64_t>(channel_count);
    params["num_pulses"] = static_cast<std::int64_t>(pulses);
    params["samples_per_pulse"] = static_cast<std::int64_t>(samples_per_pulse);
    params["threshold"] = 1.0;

    fg::PortOut<cy::common::CS16> source;
    cycore::sdk::AlgorithmBlockAdapter<PulseCompressionAlgorithm, cy::common::CS16, cy::common::CS16> pulse(params);
    cycore::sdk::AlgorithmBlockAdapter<RangeDopplerAlgorithm, cy::common::CS16, float> rd(params);
    cycore::sdk::AlgorithmBlockAdapter<CfarPlotterAlgorithm, float, std::byte> cfar(params);
    cycore::sdk::AlgorithmBlockAdapter<KalmanTrackerAlgorithm, std::byte, std::byte> tracker(params);
    fg::PortIn<std::byte> sink;

    fg::connect(source, pulse.in, element_count * 2);
    fg::connect(pulse.out, rd.in, element_count * 2);
    fg::connect(rd.out, cfar.in, element_count * 2);
    fg::connect(cfar.out, tracker.in, 4096);
    fg::connect(tracker.out, sink, 4096);

    auto input = source.reserve(element_count);
    assert(input.size() == element_count);
    for (std::size_t pulse_idx = 0; pulse_idx < pulses; ++pulse_idx) {
        for (std::size_t sample = 0; sample < samples_per_pulse; ++sample) {
            for (std::size_t channel = 0; channel < channel_count; ++channel) {
                input[CubeIndex(channel_count, samples_per_pulse, channel, pulse_idx, sample)] =
                    MakeTone(pulses, samples_per_pulse, pulse_idx, sample, range_bin, doppler_bin, 1000.0);
            }
        }
    }
    input.commit(element_count);

    pulse.work();
    rd.work();
    cfar.work();
    tracker.work();

    cycore::sdk::Reader<std::byte> reader(sink);
    auto tracks = reader.read_raw_array<cycore::algorithm::kalman_tracker::Track>();
    assert(tracks);
    assert(tracks->size() == channel_count);
    for (std::size_t i = 0; i < tracks->size(); ++i) {
        assert((*tracks)[i].range == static_cast<float>(range_bin));
        assert((*tracks)[i].velocity == static_cast<float>(doppler_bin));
        assert((*tracks)[i].power > 0.0f);
    }
    reader.consume();

    std::cout << "Radar cascade pipeline test passed." << std::endl;
    return 0;
}
