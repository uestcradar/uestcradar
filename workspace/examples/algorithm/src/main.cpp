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

uestcradar::PulseCompressionMetadata make_metadata(
    const uestcradar::IQFrameView& iq,
    std::uint64_t frame_id) {
    constexpr std::uint32_t pulses_per_cpi = 8;
    return {
        .channel_count = iq.metadata().channel_count,
        .range_bin_count = iq.metadata().samples_per_channel,
        .pulse_index = static_cast<std::uint32_t>(
            (frame_id - 1) % pulses_per_cpi),
        .pulses_per_cpi = pulses_per_cpi,
        .range_resolution_m = 1.5,
    };
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const Options options = parse_options(argc, argv);
        uestcradar::Input<uestcradar::RawFrame> input;
        uestcradar::Output<uestcradar::RawFrame> output;

        std::cout << "[algorithm] IQ -> pulse compression ready"
                  << " frames=" << options.frames << '\n';

        for (std::uint64_t processed = 1;
             options.frames == 0 || processed <= options.frames;
             ++processed) {
            uestcradar::RawFrame input_frame = input.read();
            auto iq = uestcradar::IQFrameView::from(input_frame);

            const auto metadata =
                make_metadata(iq, input_frame.envelope().frame_id);
            uestcradar::RawFrame output_frame = output.create({
                .frame_id = input_frame.envelope().frame_id,
                .timestamp = input_frame.envelope().timestamp,
                .type_id =
                    uestcradar::PulseCompressionFrameView::type_id,
                .type_version =
                    uestcradar::PulseCompressionFrameView::type_version,
                .payload_length = static_cast<std::uint32_t>(
                    uestcradar::PulseCompressionFrameView::payload_bytes(
                        metadata)),
            });
            auto pulse =
                uestcradar::PulseCompressionFrameView::initialize(
                    output_frame, metadata);

            const auto result =
                radar_algorithm::pulse_compress(iq.data(), pulse.data());
            if (processed == 1 || processed % options.log_every == 0) {
                std::cout << "[algorithm] processed=" << processed
                          << " frame=" << input_frame.envelope().frame_id
                          << " iq=" << iq.data().rows() << 'x'
                          << iq.data().columns()
                          << " pulse=" << pulse.data().rows() << 'x'
                          << pulse.data().columns()
                          << " peak_range_bin=" << result.peak_range_bin
                          << '\n';
            }

            output.write(std::move(output_frame));
        }

        std::cout << "[algorithm] completed frames=" << options.frames
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[algorithm] error=" << error.what() << '\n';
        return 1;
    }
}
