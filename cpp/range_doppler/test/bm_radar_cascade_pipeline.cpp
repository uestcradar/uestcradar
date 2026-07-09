#include <pulse_compression_algorithm.h>
#include <range_doppler_algorithm.h>
#include <cfar_plotter_algorithm.h>
#include <kalman_tracker_algorithm.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <flowgraph/port.h>
#include <flowgraph/value.h>

namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kDefaultNumChannels = 16;
constexpr std::size_t kDefaultNumPulses = 64;
constexpr std::size_t kDefaultSamplesPerPulse = 512;

struct Config {
    std::size_t channels = kDefaultNumChannels;
    std::size_t pulses = kDefaultNumPulses;
    std::size_t samples_per_pulse = kDefaultSamplesPerPulse;
    std::size_t iterations = 50;
    std::size_t range_bin = 37;
    std::size_t doppler_bin = 9;
};

struct LatencyRecorder {
    std::vector<double> samples_us;

    explicit LatencyRecorder(std::size_t expected_count) {
        samples_us.reserve(expected_count);
    }

    void add(std::chrono::steady_clock::time_point begin,
             std::chrono::steady_clock::time_point end) {
        samples_us.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    }

    double avg() const {
        double sum = 0.0;
        for (double value : samples_us) {
            sum += value;
        }
        return samples_us.empty() ? 0.0 : sum / static_cast<double>(samples_us.size());
    }

    double percentile(double p) const {
        if (samples_us.empty()) {
            return 0.0;
        }
        auto sorted = samples_us;
        std::sort(sorted.begin(), sorted.end());
        const auto index = static_cast<std::size_t>(
            std::min<double>(sorted.size() - 1,
                             (p / 100.0) * static_cast<double>(sorted.size() - 1)));
        return sorted[index];
    }
};

std::size_t ParseSizeArg(int argc, char** argv, int index, std::size_t fallback) {
    if (argc <= index) {
        return fallback;
    }
    const auto parsed = std::stoull(argv[index]);
    if (parsed == 0) {
        throw std::invalid_argument("benchmark dimensions must be positive");
    }
    return static_cast<std::size_t>(parsed);
}

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

std::size_t CubeElements(std::size_t channels,
                         std::size_t pulses,
                         std::size_t samples_per_pulse) {
    if (channels != 0 && pulses > std::numeric_limits<std::size_t>::max() / channels) {
        throw std::overflow_error("radar cube element count overflow");
    }
    const std::size_t channel_pulses = channels * pulses;
    if (channel_pulses != 0 &&
        samples_per_pulse > std::numeric_limits<std::size_t>::max() / channel_pulses) {
        throw std::overflow_error("radar cube element count overflow");
    }
    return channel_pulses * samples_per_pulse;
}

cy::common::CS16 MakeTone(const Config& config,
                          std::size_t channel,
                          std::size_t pulse,
                          std::size_t sample) {
    const double phase = 2.0 * kPi *
                             (static_cast<double>(config.range_bin) * static_cast<double>(sample) /
                                  static_cast<double>(config.samples_per_pulse) +
                              static_cast<double>(config.doppler_bin) * static_cast<double>(pulse) /
                                  static_cast<double>(config.pulses)) +
                         0.05 * static_cast<double>(channel);
    constexpr double kAmplitude = 1000.0;
    return cy::common::CS16{
        Quantize(kAmplitude * std::cos(phase)),
        Quantize(kAmplitude * std::sin(phase))};
}

fg::ValueMap MakeParams(const Config& config) {
    fg::ValueMap params;
    params["num_channels"] = static_cast<std::int64_t>(config.channels);
    params["num_pulses"] = static_cast<std::int64_t>(config.pulses);
    params["samples_per_pulse"] = static_cast<std::int64_t>(config.samples_per_pulse);
    params["threshold"] = 1.0;
    return params;
}

void PublishSource(fg::PortOut<cy::common::CS16>& source,
                   const Config& config,
                   std::size_t element_count) {
    auto input = source.reserve(element_count);
    if (input.size() != element_count) {
        throw std::runtime_error("source could not reserve a full radar cube");
    }

    for (std::size_t pulse = 0; pulse < config.pulses; ++pulse) {
        for (std::size_t sample = 0; sample < config.samples_per_pulse; ++sample) {
            for (std::size_t channel = 0; channel < config.channels; ++channel) {
                input[CubeIndex(config.channels, config.samples_per_pulse, channel, pulse, sample)] =
                    MakeTone(config, channel, pulse, sample);
            }
        }
    }
    input.commit(element_count);
}

void DrainTracks(fg::PortIn<std::byte>& sink, const Config& config) {
    sdk::Reader<std::byte> reader(sink);
    auto tracks = reader.read_raw_array<cycore::algorithm::kalman_tracker::Track>();
    if (!tracks) {
        throw std::runtime_error("tracker produced no RawBytes track frame");
    }
    if (tracks->size() != config.channels) {
        throw std::runtime_error("tracker output count does not match channel count");
    }
    for (std::size_t i = 0; i < tracks->size(); ++i) {
        assert((*tracks)[i].range == static_cast<float>(config.range_bin));
        assert((*tracks)[i].velocity == static_cast<float>(config.doppler_bin));
        assert((*tracks)[i].power > 0.0f);
    }
    reader.consume();
}

template <typename Block>
void MeasureWork(Block& block, LatencyRecorder& recorder) {
    const auto begin = std::chrono::steady_clock::now();
    block.work();
    const auto end = std::chrono::steady_clock::now();
    recorder.add(begin, end);
}

void PrintLatency(const char* name, const LatencyRecorder& recorder) {
    std::cout << std::left
              << std::setw(20) << name
              << std::setw(14) << recorder.avg()
              << std::setw(14) << recorder.percentile(50.0)
              << std::setw(14) << recorder.percentile(95.0)
              << recorder.percentile(99.0)
              << '\n';
}

} // namespace

int main(int argc, char** argv) {
    Config config;
    config.channels = ParseSizeArg(argc, argv, 1, config.channels);
    config.pulses = ParseSizeArg(argc, argv, 2, config.pulses);
    config.samples_per_pulse = ParseSizeArg(argc, argv, 3, config.samples_per_pulse);
    config.iterations = ParseSizeArg(argc, argv, 4, config.iterations);
    config.range_bin = ParseSizeArg(argc, argv, 5, config.range_bin) % config.samples_per_pulse;
    config.doppler_bin = ParseSizeArg(argc, argv, 6, config.doppler_bin) % config.pulses;

    const std::size_t element_count =
        CubeElements(config.channels, config.pulses, config.samples_per_pulse);
    const auto params = MakeParams(config);

    fg::PortOut<cy::common::CS16> source;
    sdk::AlgorithmBlockAdapter<PulseCompressionAlgorithm, cy::common::CS16, cy::common::CS16> pulse(params);
    sdk::AlgorithmBlockAdapter<RangeDopplerAlgorithm, cy::common::CS16, float> rd(params);
    sdk::AlgorithmBlockAdapter<CfarPlotterAlgorithm, float, std::byte> cfar(params);
    sdk::AlgorithmBlockAdapter<KalmanTrackerAlgorithm, std::byte, std::byte> tracker(params);
    fg::PortIn<std::byte> sink;

    const std::size_t frame_slots = config.iterations + 1;
    if (element_count > std::numeric_limits<std::size_t>::max() / frame_slots) {
        throw std::overflow_error("typed benchmark capacity overflow");
    }
    const std::size_t typed_capacity = std::max<std::size_t>(element_count * frame_slots, 1);
    const std::size_t max_raw_frame = std::max(
        sdk::RawArrayFrameBytes<cycore::algorithm::cfar_plotter::Plot>(config.channels),
        sdk::RawArrayFrameBytes<cycore::algorithm::kalman_tracker::Track>(config.channels));
    if (max_raw_frame > std::numeric_limits<std::size_t>::max() / frame_slots) {
        throw std::overflow_error("raw benchmark capacity overflow");
    }
    const std::size_t raw_capacity = std::max<std::size_t>(
        max_raw_frame * frame_slots,
        4096);
    fg::connect(source, pulse.in, typed_capacity);
    fg::connect(pulse.out, rd.in, typed_capacity);
    fg::connect(rd.out, cfar.in, typed_capacity);
    fg::connect(cfar.out, tracker.in, raw_capacity);
    fg::connect(tracker.out, sink, raw_capacity);

    LatencyRecorder pulse_latency(config.iterations);
    LatencyRecorder rd_latency(config.iterations);
    LatencyRecorder cfar_latency(config.iterations);
    LatencyRecorder tracker_latency(config.iterations);

    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < config.iterations; ++i) {
        PublishSource(source, config, element_count);
        MeasureWork(pulse, pulse_latency);
        MeasureWork(rd, rd_latency);
        MeasureWork(cfar, cfar_latency);
        MeasureWork(tracker, tracker_latency);
        DrainTracks(sink, config);
    }
    const auto end = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(end - begin).count();
    const double frames_per_second = static_cast<double>(config.iterations) / seconds;
    const double input_gib_per_second =
        (static_cast<double>(config.iterations * element_count * sizeof(cy::common::CS16)) /
         (1024.0 * 1024.0 * 1024.0)) /
        seconds;

    std::cout << "Radar cascade benchmark\n"
              << "channels=" << config.channels
              << " pulses=" << config.pulses
              << " samples_per_pulse=" << config.samples_per_pulse
              << " iterations=" << config.iterations
              << " range_bin=" << config.range_bin
              << " doppler_bin=" << config.doppler_bin
              << '\n'
              << "frames/s=" << frames_per_second
              << " input_GiB/s=" << input_gib_per_second
              << '\n'
              << std::left
              << std::setw(20) << "Stage"
              << std::setw(14) << "avg_us"
              << std::setw(14) << "p50_us"
              << std::setw(14) << "p95_us"
              << "p99_us"
              << '\n';

    PrintLatency("pulse_compression", pulse_latency);
    PrintLatency("range_doppler", rd_latency);
    PrintLatency("cfar_plotter", cfar_latency);
    PrintLatency("kalman_tracker", tracker_latency);
    return 0;
}
