#include <data.h>

#include "my_waveform.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

struct Options {
    std::string type{"iq"};
    std::uint64_t frames{0};
    double rate_hz{20.0};
};

std::uint64_t parse_uint64(const char* value, const char* option) {
    char* end = nullptr;
    const auto result = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return result;
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

void send_iq(uestcradar::Output<uestcradar::IQFrame>& output) {
    const auto metadata = radar_example::iq_metadata();
    auto iq = output.create(metadata);
    radar_example::fill_iq(iq);
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
    const auto interval = std::chrono::duration<double>(
        1.0 / options.rate_hz);
    for (std::uint64_t sequence = 1;
         options.frames == 0 || sequence <= options.frames;
         ++sequence) {
        send(sequence);
        if (sequence == 1 || sequence % 20 == 0) {
            std::cout << "[source] sent type=" << options.type
                      << " sequence=" << sequence << '\n';
        }
        std::this_thread::sleep_for(interval);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const Options options = parse_options(argc, argv);
        std::cout << "[source] type=" << options.type
                  << " rate_hz=" << options.rate_hz
                  << " frames=" << options.frames << '\n';

        if (options.type == "iq") {
            uestcradar::Output<uestcradar::IQFrame> output;
            run_source(options, [&](std::uint64_t) { send_iq(output); });
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
