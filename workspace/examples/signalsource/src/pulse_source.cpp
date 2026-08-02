#include <data.h>

#include "my_waveform.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace {

struct Options {
    std::uint64_t frames{0};
    double rate_hz{30.0};
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--frames" && index + 1 < argc) {
            options.frames = std::strtoull(argv[++index], nullptr, 10);
        } else if (argument == "--rate-hz" && index + 1 < argc) {
            options.rate_hz = std::stod(argv[++index]);
        } else {
            throw std::invalid_argument("unknown or incomplete option");
        }
    }
    if (options.rate_hz <= 0.0) {
        throw std::invalid_argument("rate must be positive");
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const Options options = parse_options(argc, argv);
        uestcradar::Output<uestcradar::PulseCompressionFrame> output;
        const auto interval = std::chrono::duration_cast<
            std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / options.rate_hz));
        auto next = std::chrono::steady_clock::now();
        for (std::uint64_t sequence = 1;
             options.frames == 0 || sequence <= options.frames;
             ++sequence) {
            auto frame = output.create(
                radar_example::pulse_metadata(sequence));
            radar_example::fill_pulse(frame);
            output.write(std::move(frame));
            next += interval;
            std::this_thread::sleep_until(next);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[pulse-source] error=" << error.what() << '\n';
        return 1;
    }
}
