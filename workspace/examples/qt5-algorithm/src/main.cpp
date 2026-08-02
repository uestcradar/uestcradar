#include <data.h>

#include "my_rd_algorithm.hpp"

#include <QCoreApplication>
#include <QDebug>

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

struct Options {
    std::uint64_t frames{0};
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
    QCoreApplication application(argc, argv);
    try {
        const Options options = parse_options(argc, argv);
        uestcradar::Input<uestcradar::PulseCompressionFrame> input;
        uestcradar::Output<uestcradar::RDFrame> output;
        radar_qt_example::CpiBuffer cpi;

        qInfo() << "[qt-algorithm] pulse compression -> RD ready"
                << "frames=" << options.frames;

        std::uint64_t produced = 0;
        while (options.frames == 0 || produced < options.frames) {
            auto pulse = input.read();
            const auto pulse_metadata = pulse.metadata();
            cpi.push(
                pulse_metadata.pulse_index,
                pulse_metadata.pulses_per_cpi,
                pulse.data()[0]);

            if (!cpi.ready()) {
                continue;
            }

            const uestcradar::RDMetadata metadata{
                .channel_index = 0,
                .range_bin_count = static_cast<std::uint32_t>(
                    cpi.range_bin_count()),
                .doppler_bin_count = radar_qt_example::kPulsesPerCpi,
                .range_resolution_m = pulse_metadata.range_resolution_m,
                .velocity_resolution_mps = 0.5,
            };
            auto rd = output.create(metadata, pulse);
            const auto result =
                radar_qt_example::compute_rd(cpi, rd.data());

            ++produced;
            if (produced == 1 || produced % options.log_every == 0) {
                qInfo() << "[qt-algorithm] produced=" << produced
                        << "RD=" << rd.data().rows() << "x"
                        << rd.data().columns()
                        << "peak=" << result.peak_range_bin << ","
                        << result.peak_doppler_bin;
            }

            output.write(std::move(rd));
            cpi.clear();
        }

        qInfo() << "[qt-algorithm] completed frames=" << produced;
        return 0;
    } catch (const std::exception& error) {
        qCritical() << "[qt-algorithm] error=" << error.what();
        return 1;
    }
}
