#include <data.h>

#include "iq_adapter.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

struct Options {
    std::uint64_t cpis{0};
    std::uint64_t log_every{1};
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
        if ((argument == "--cpis" || argument == "--frames") &&
            index + 1 < argc) {
            options.cpis = parse_uint64(argv[++index]);
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

void write_pulse_frames(
    const uestcradar::IQFrame& parent,
    const radar_algorithm::AdaptedCpi& adapted,
    const EchoProcessOutput& result,
    uestcradar::Output<uestcradar::PulseCompressionFrame>& output) {
    if (result.pulseCompressed.size() !=
        static_cast<std::size_t>(adapted.params.pulseN)) {
        throw std::runtime_error("original algorithm returned wrong pulse count");
    }
    const double range_resolution_m =
        adapted.params.c / (2.0 * adapted.params.fs);
    for (std::size_t pulse_index = 0;
         pulse_index < result.pulseCompressed.size();
         ++pulse_index) {
        const auto& values = result.pulseCompressed[pulse_index];
        if (values.empty() ||
            values.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("pulse-compression row size is invalid");
        }
        const uestcradar::PulseCompressionMetadata metadata{
            .channel_count = 1,
            .range_bin_count = static_cast<std::uint32_t>(values.size()),
            .pulse_index = static_cast<std::uint32_t>(pulse_index),
            .pulses_per_cpi =
                static_cast<std::uint32_t>(result.pulseCompressed.size()),
            .range_resolution_m = range_resolution_m,
        };
        auto frame = output.create(metadata, parent);
        auto destination = frame.data()[0];
        for (std::size_t index = 0; index < values.size(); ++index) {
            destination[index] = {
                static_cast<float>(values[index].real()),
                static_cast<float>(values[index].imag()),
            };
        }
        output.write(std::move(frame));
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const Options options = parse_options(argc, argv);
        uestcradar::Input<uestcradar::IQFrame> input;
        uestcradar::Output<uestcradar::PulseCompressionFrame> output;
        std::cout << "[algorithm] complete CPI IQ v3 adapter ready"
                  << " cpis=" << options.cpis << '\n';

        for (std::uint64_t processed = 1;
             options.cpis == 0 || processed <= options.cpis;
             ++processed) {
            auto iq = input.read();
            const auto adapted = radar_algorithm::adapt_complete_cpi(
                iq.metadata(),
                uestcradar::Array2D<const uestcradar::ComplexInt16>{
                    iq.data().values().data(),
                    iq.data().rows(),
                    iq.data().columns(),
                });
            const auto result =
                radar_algorithm::run_original_algorithm(adapted);
            write_pulse_frames(iq, adapted, result, output);
            if (processed == 1 || processed % options.log_every == 0) {
                std::cout << "[algorithm] processed_cpi=" << processed
                          << " input_samples=" << adapted.input.echo.size()
                          << " output_pulses="
                          << result.pulseCompressed.size()
                          << " peak_range=" << result.peakRange
                          << " peak_velocity=" << result.peakVelocity << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[algorithm] error=" << error.what() << '\n';
        return 1;
    }
}
