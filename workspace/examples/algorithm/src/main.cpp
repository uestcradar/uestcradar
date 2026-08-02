#include <data.h>

#include "my_algorithm.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

struct Options {
    std::uint64_t frames{0};
    std::uint64_t log_every{20};
};

std::uint64_t parse_uint64(const char* value) {
    char* end = nullptr;
    const auto result = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        throw std::invalid_argument("invalid integer option");
    }
    return result;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--frames" && index + 1 < argc) {
            options.frames = parse_uint64(argv[++index]);
        } else if (argument == "--log-every" && index + 1 < argc) {
            options.log_every = parse_uint64(argv[++index]);
        } else {
            throw std::invalid_argument("unknown or incomplete option");
        }
    }
    if (options.log_every == 0) {
        throw std::invalid_argument("log interval must be positive");
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const Options options = parse_options(argc, argv);
        uestcradar::Input<uestcradar::IQFrame> input;
        uestcradar::Output<uestcradar::PulseCompressionFrame> output;

        std::cout << "[algorithm] IQ -> pulse compression ready"
                  << " frames=" << options.frames << '\n';

        for (std::uint64_t processed = 1;
             options.frames == 0 || processed <= options.frames;
             ++processed) {
            auto iq = input.read();
            const auto iq_metadata = iq.metadata();
            constexpr std::uint32_t pulses_per_cpi = 8;
            const uestcradar::PulseCompressionMetadata metadata{
                .channel_count = iq_metadata.channel_count,
                .range_bin_count = iq_metadata.samples_per_channel,
                .pulse_index = static_cast<std::uint32_t>(
                    (processed - 1) % pulses_per_cpi),
                .pulses_per_cpi = pulses_per_cpi,
                .range_resolution_m = 1.5,
            };
            auto pulse = output.create(metadata, iq);

            const auto result =
                radar_algorithm::pulse_compress(iq.data(), pulse.data());
            if (processed == 1 || processed % options.log_every == 0) {
                std::cout << "[algorithm] processed=" << processed
                          << " iq=" << iq.data().rows() << 'x'
                          << iq.data().columns()
                          << " pulse=" << pulse.data().rows() << 'x'
                          << pulse.data().columns()
                          << " peak_range_bin=" << result.peak_range_bin
                          << '\n';
            }

            output.write(std::move(pulse));
        }

        std::cout << "[algorithm] completed frames=" << options.frames
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[algorithm] error=" << error.what() << '\n';
        return 1;
    }
}
