#include <data.h>

#include "my_waveform.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct Options {
    std::string type{"iq"};
    std::uint64_t frames{0};
    double rate_hz{30.0};
    std::uint32_t channels{4};
    std::uint32_t samples_per_channel{1'277'952};
};

std::uint64_t parse_uint64(const char* value, const char* option) {
    char* end = nullptr;
    const auto result = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return result;
}

std::uint32_t parse_uint32(const char* value, const char* option) {
    const auto result = parse_uint64(value, option);
    if (result == 0 || result > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return static_cast<std::uint32_t>(result);
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--type" && index + 1 < argc) {
            options.type = argv[++index];
        } else if (argument == "--frames" && index + 1 < argc) {
            options.frames = parse_uint64(argv[++index], "--frames");
        } else if (argument == "--rate-hz" && index + 1 < argc) {
            options.rate_hz = std::stod(argv[++index]);
        } else if (argument == "--channels" && index + 1 < argc) {
            options.channels = parse_uint32(argv[++index], "--channels");
        } else if (argument == "--samples-per-channel" && index + 1 < argc) {
            options.samples_per_channel = parse_uint32(
                argv[++index], "--samples-per-channel");
        } else {
            throw std::invalid_argument("unknown or incomplete option");
        }
    }
    if ((options.type != "iq" && options.type != "pulse") ||
        options.rate_hz <= 0.0) {
        throw std::invalid_argument("type or rate is invalid");
    }
    return options;
}

void send_iq(
    uestcradar::Output<uestcradar::IQFrame>& output,
    const Options& options,
    const std::vector<uestcradar::ComplexInt16>& waveform) {
    const auto metadata = radar_example::iq_metadata(
        options.channels, options.samples_per_channel);
    auto iq = output.create(metadata);
    std::copy(waveform.begin(), waveform.end(), iq.data().values().begin());
    output.write(std::move(iq));
}

void send_pulse(
    uestcradar::Output<uestcradar::PulseCompressionFrame>& output,
    std::uint64_t sequence) {
    const auto metadata = radar_example::pulse_metadata(sequence);
    auto pulse = output.create(metadata);
    radar_example::fill_pulse(pulse);
    output.write(std::move(pulse));
}

template <class Send>
void run_source(const Options& options, Send send) {
    const auto interval = std::chrono::duration_cast<
        std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / options.rate_hz));
    auto next = std::chrono::steady_clock::now();
    for (std::uint64_t sequence = 1;
         options.frames == 0 || sequence <= options.frames;
         ++sequence) {
        send(sequence);
        if (sequence == 1 || sequence % 20 == 0) {
            std::cout << "[source] sent type=" << options.type
                      << " sequence=" << sequence << '\n';
        }
        next += interval;
        std::this_thread::sleep_until(next);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const Options options = parse_options(argc, argv);
        std::cout << "[source] type=" << options.type
                  << " rate_hz=" << options.rate_hz
                  << " frames=" << options.frames
                  << " channels=" << options.channels
                  << " samples_per_channel="
                  << options.samples_per_channel << '\n';

        if (options.type == "iq") {
            uestcradar::Output<uestcradar::IQFrame> output;
            const auto waveform = radar_example::make_iq_waveform(
                options.channels, options.samples_per_channel);
            run_source(options, [&](std::uint64_t) {
                send_iq(output, options, waveform);
            });
        } else {
            uestcradar::Output<uestcradar::PulseCompressionFrame> output;
            run_source(options, [&](std::uint64_t sequence) {
                send_pulse(output, sequence);
            });
        }

        std::cout << "[source] completed frames=" << options.frames << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[source] error=" << error.what() << '\n';
        return 1;
    }
}
