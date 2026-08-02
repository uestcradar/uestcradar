#include <data.h>

#include "cpi_data.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct Options {
    std::filesystem::path data_root{"/data"};
    std::uint64_t frames{0};
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
        if (argument == "--data-root" && index + 1 < argc) {
            options.data_root = argv[++index];
        } else if (argument == "--data-dir" && index + 1 < argc) {
            const std::filesystem::path legacy = argv[++index];
            options.data_root = legacy.filename() == "CPI0"
                ? legacy.parent_path()
                : legacy;
        } else if (argument == "--frames" && index + 1 < argc) {
            options.frames = parse_uint64(argv[++index], "--frames");
        } else {
            throw std::invalid_argument("unknown or incomplete option");
        }
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const Options options = parse_options(argc, argv);
        const auto cpis = radar_example::load_cpi_sequence(options.data_root);
        uestcradar::Output<uestcradar::IQFrame> output;

        std::cout << "[source] IQ v3 offline CPI sequence ready"
                  << " data_root=" << options.data_root
                  << " offline_cpis=" << cpis.size()
                  << " samples=" << cpis.front().metadata.samples_per_channel
                  << " pulses=" << cpis.front().metadata.pulse_count
                  << " frames=" << options.frames << '\n';

        std::uint64_t sent = 0;
        while (options.frames == 0 || sent < options.frames) {
            const std::size_t offline_index =
                static_cast<std::size_t>(sent % cpis.size());
            const auto& cpi = cpis[offline_index];
            auto metadata = cpi.metadata;
            metadata.cpi_index = sent;
            auto frame = output.create(metadata);
            radar_example::copy_cpi_samples(cpi, frame);
            output.write(std::move(frame));
            if (sent == 0 || (sent + 1) % 20 == 0) {
                std::cout << "[source] sent_frames=" << sent + 1
                          << " cpi_index=" << sent
                          << " offline_cpi=CPI" << offline_index
                          << " cs16_bytes=" << cpi.cs16.size() << '\n';
            }
            if (sent == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("global cpi_index exhausted");
            }
            ++sent;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[source] error=" << error.what() << '\n';
        return 1;
    }
}
