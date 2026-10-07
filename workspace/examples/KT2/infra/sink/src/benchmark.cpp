#include "detection_executor.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t threads{4};
    std::size_t queue_depth{8};
    std::size_t bins{22196};
    double warmup_seconds{2.0};
    double seconds{10.0};
};

std::uint64_t parse_uint64(const char* value, const char* option) {
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return parsed;
}

double parse_double(const char* value, const char* option) {
    char* end = nullptr;
    const auto parsed = std::strtod(value, &end);
    if (end == value || *end != '\0' || !std::isfinite(parsed) ||
        parsed <= 0.0) {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    Options options;
    bool queue_depth_set = false;
    for (int index = 1; index < argc; ++index) {
        if (index + 1 >= argc) {
            throw std::invalid_argument("incomplete option");
        }
        const std::string_view argument{argv[index]};
        const char* value = argv[++index];
        if (argument == "--threads") {
            options.threads = static_cast<std::size_t>(
                parse_uint64(value, "--threads"));
        } else if (argument == "--queue-depth") {
            options.queue_depth = static_cast<std::size_t>(
                parse_uint64(value, "--queue-depth"));
            queue_depth_set = true;
        } else if (argument == "--bins") {
            options.bins = static_cast<std::size_t>(
                parse_uint64(value, "--bins"));
        } else if (argument == "--warmup-seconds") {
            options.warmup_seconds = parse_double(value, "--warmup-seconds");
        } else if (argument == "--seconds") {
            options.seconds = parse_double(value, "--seconds");
        } else {
            throw std::invalid_argument("unknown option");
        }
    }
    if (!queue_depth_set) {
        options.queue_depth = options.threads * 2;
    }
    if (options.threads == 0 || options.queue_depth < options.threads) {
        throw std::invalid_argument(
            "threads must be positive and queue depth must cover them");
    }
    if (options.bins <= 20480) {
        throw std::invalid_argument("--bins must include target range 20480");
    }
    return options;
}

std::vector<uestcradar::ComplexFloat32> make_frame(std::size_t bins) {
    std::vector<uestcradar::ComplexFloat32> frame(bins);
    std::uint32_t state = 0x12345678U;
    for (auto& sample : frame) {
        state = state * 1664525U + 1013904223U;
        const float noise_i =
            static_cast<float>(state & 0xffffU) / 65535.0F - 0.5F;
        state = state * 1664525U + 1013904223U;
        const float noise_q =
            static_cast<float>(state & 0xffffU) / 65535.0F - 0.5F;
        sample = {noise_i, noise_q};
    }
    frame[20480] = {20.0F, 0.0F};
    return frame;
}

struct Measurement {
    std::uint64_t frames{};
    double seconds{};
    double mb_per_second{};
    double frames_per_second{};
};

Measurement run_phase(
    double duration_seconds,
    const Options& options,
    std::span<const uestcradar::ComplexFloat32> frame) {
    radar_sink::TargetConfig config;
    radar_sink::DetectionExecutor executor(
        options.threads, options.queue_depth, config);
    const uestcradar::PulseCompressionMetadata metadata{
        .channel_count = 1,
        .range_bin_count = static_cast<std::uint32_t>(frame.size()),
        .pulse_index = 0,
        .pulses_per_cpi = config.pulses_per_cpi,
        .range_resolution_m = 1.0,
    };

    const auto started = Clock::now();
    const auto deadline = started + std::chrono::duration<double>(
        duration_seconds);
    std::uint64_t submitted = 0;
    std::uint64_t next_result = 0;
    std::size_t in_flight = 0;
    while (Clock::now() < deadline) {
        auto frame_metadata = metadata;
        frame_metadata.pulse_index = static_cast<std::uint32_t>(
            submitted % config.pulses_per_cpi);
        executor.submit(
            submitted,
            submitted + 1,
            frame_metadata,
            1,
            frame.size(),
            frame);
        ++submitted;
        ++in_flight;
        if (in_flight == executor.queue_depth()) {
            const auto result = executor.take(next_result++);
            if (!result.detection.detected ||
                result.detection.target_peak_bin != config.range_bin) {
                throw std::runtime_error("benchmark detection mismatch");
            }
            --in_flight;
        }
    }
    while (in_flight > 0) {
        const auto result = executor.take(next_result++);
        if (!result.detection.detected ||
            result.detection.target_peak_bin != config.range_bin) {
            throw std::runtime_error("benchmark detection mismatch");
        }
        --in_flight;
    }
    const double elapsed = std::chrono::duration<double>(
        Clock::now() - started).count();
    const double bytes = static_cast<double>(submitted) *
        static_cast<double>(frame.size()) *
        static_cast<double>(sizeof(frame.front()));
    return {
        .frames = submitted,
        .seconds = elapsed,
        .mb_per_second = bytes / elapsed / 1'000'000.0,
        .frames_per_second = static_cast<double>(submitted) / elapsed,
    };
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        const auto frame = make_frame(options.bins);
        static_cast<void>(run_phase(options.warmup_seconds, options, frame));
        const auto result = run_phase(options.seconds, options, frame);
        std::cout << std::fixed << std::setprecision(2)
                  << "BENCHMARK PASS"
                  << " threads=" << options.threads
                  << " queue_depth=" << options.queue_depth
                  << " bins=" << options.bins
                  << " frames=" << result.frames
                  << " seconds=" << result.seconds
                  << " fps=" << result.frames_per_second
                  << " payload_mb_s=" << result.mb_per_second << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "BENCHMARK FAIL error=" << error.what() << '\n';
        return 1;
    }
}
