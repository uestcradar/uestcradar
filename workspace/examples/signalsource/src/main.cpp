#include <data.h>

#include "cpi_data.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace {

struct Options {
    std::filesystem::path data_dir{"/data/CPI0"};
    std::uint64_t frames{0};
    double rate_hz{30.0};
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
        if (argument == "--data-dir" && index + 1 < argc) {
            options.data_dir = argv[++index];
        } else if (argument == "--frames" && index + 1 < argc) {
            options.frames = parse_uint64(argv[++index], "--frames");
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
        const radar_example::CpiData cpi =
            radar_example::load_cpi(options.data_dir);
        uestcradar::Output<uestcradar::IQFrame> output;

        std::cout << "[source] IQ v3 complete CPI"
                  << " data_dir=" << options.data_dir
                  << " cpi_index=" << cpi.metadata.cpi_index
                  << " samples=" << cpi.metadata.samples_per_channel
                  << " pulses=" << cpi.metadata.pulse_count
                  << " rate_hz=" << options.rate_hz
                  << " frames=" << options.frames << '\n';

        const auto interval = std::chrono::duration_cast<
            std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / options.rate_hz));
        auto next = std::chrono::steady_clock::now();
        for (std::uint64_t sequence = 1;
             options.frames == 0 || sequence <= options.frames;
             ++sequence) {
            auto frame = output.create(cpi.metadata);
            radar_example::copy_cpi_samples(cpi, frame);
            output.write(std::move(frame));
            if (sequence == 1 || sequence % 20 == 0) {
                std::cout << "[source] sent complete_cpi=" << sequence
                          << " cs16_bytes=" << cpi.cs16.size() << '\n';
            }
            next += interval;
            std::this_thread::sleep_until(next);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[source] error=" << error.what() << '\n';
        return 1;
    }
}
