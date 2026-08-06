#include <data.h>

#include "target_validator.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::uint64_t frames{0};
    std::uint64_t log_every{10};
    radar_sink::TargetConfig target;
};

std::uint64_t parse_uint64(const char* value, const char* option) {
    char* end = nullptr;
    const auto result = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return result;
}

double parse_double(const char* value, const char* option) {
    char* end = nullptr;
    const auto result = std::strtod(value, &end);
    if (end == value || *end != '\0' || !std::isfinite(result)) {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return result;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--fail-on-target-miss") {
            options.target.fail_on_target_miss = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("incomplete option");
        }
        const char* value = argv[++index];
        if (argument == "--frames") {
            options.frames = parse_uint64(value, "--frames");
        } else if (argument == "--log-every") {
            options.log_every = parse_uint64(value, "--log-every");
        } else if (argument == "--target-range") {
            options.target.range_bin = static_cast<std::size_t>(
                parse_uint64(value, "--target-range"));
        } else if (argument == "--target-half-width") {
            options.target.half_width = static_cast<std::size_t>(
                parse_uint64(value, "--target-half-width"));
        } else if (argument == "--target-min-snr-db") {
            options.target.minimum_snr_db = parse_double(
                value, "--target-min-snr-db");
        } else if (argument == "--pulses-per-cpi") {
            const auto parsed = parse_uint64(value, "--pulses-per-cpi");
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--pulses-per-cpi is too large");
            }
            options.target.pulses_per_cpi =
                static_cast<std::uint32_t>(parsed);
        } else {
            throw std::invalid_argument("unknown option");
        }
    }
    if (options.log_every == 0 || options.target.pulses_per_cpi == 0) {
        throw std::invalid_argument("log interval and CPI size must be positive");
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const Options options = parse_options(argc, argv);
        radar_sink::CpiTracker tracker(options.target);
        uestcradar::Input<uestcradar::PulseCompressionFrame> input;

        std::cout << "[sink] waiting for PulseCompressionFrame v2"
                  << " frames=" << options.frames
                  << " target_range=" << options.target.range_bin
                  << " half_width=" << options.target.half_width
                  << " min_snr_db=" << options.target.minimum_snr_db
                  << " pulses_per_cpi=" << options.target.pulses_per_cpi
                  << " strict="
                  << (options.target.fail_on_target_miss ? "yes" : "no")
                  << '\n';

        std::uint64_t total_pulses = 0;
        std::uint64_t total_detected = 0;
        for (std::uint64_t received = 1;
             options.frames == 0 || received <= options.frames;
             ++received) {
            auto frame = input.read();
            const auto metadata = frame.metadata();
            radar_sink::TargetDetection detection;
            if (metadata.pulses_per_cpi == options.target.pulses_per_cpi) {
                if (frame.data().rows() == 0) {
                    throw std::runtime_error(
                        "PulseCompression frame has no data channel");
                }
                detection = radar_sink::detect_target(
                    frame.data()[0], options.target);
            }

            const auto observation = tracker.observe(
                metadata.pulse_index,
                metadata.pulses_per_cpi,
                detection);
            if (observation.kind == radar_sink::ObservationKind::skipped) {
                if (received == 1 || received % options.log_every == 0) {
                    std::cout << "[sink] target_validation=SKIPPED"
                              << " received=" << received
                              << " reason=" << observation.reason
                              << " pulse_index=" << metadata.pulse_index
                              << " pulses_per_cpi="
                              << metadata.pulses_per_cpi << '\n';
                }
                continue;
            }

            if (received == 1 || received % options.log_every == 0) {
                std::cout << "[sink] received=" << received
                          << " shape=" << frame.data().rows() << 'x'
                          << frame.data().columns()
                          << " pulse_index=" << metadata.pulse_index
                          << " global_peak_bin="
                          << detection.global_peak_bin
                          << " target_peak_bin="
                          << detection.target_peak_bin
                          << " target_snr_db=" << detection.snr_db
                          << " target_detected="
                          << (detection.detected ? "yes" : "no") << '\n';
            }

            if (observation.kind != radar_sink::ObservationKind::summary) {
                continue;
            }
            const auto& summary = *observation.summary;
            total_pulses += summary.pulses;
            total_detected += summary.detected;
            const auto missed = summary.pulses - summary.detected;
            const bool passed = missed == 0;
            std::cout << "[sink] target_summary cpi=" << summary.cpi_index
                      << " pulses=" << summary.pulses
                      << " detected=" << summary.detected
                      << " missed=" << missed
                      << " expected_range=" << options.target.range_bin
                      << " half_width=" << options.target.half_width
                      << " min_snr_db=" << summary.minimum_snr_db
                      << " threshold_snr_db="
                      << options.target.minimum_snr_db
                      << " status=" << (passed ? "PASS" : "FAIL") << '\n';
            if (!passed && options.target.fail_on_target_miss) {
                throw std::runtime_error(
                    "target missing from one or more pulses");
            }
        }

        if (tracker.has_partial_cpi() &&
            options.target.fail_on_target_miss) {
            throw std::runtime_error("input ended with a partial CPI");
        }
        if (options.frames != 0) {
            std::cout << "[sink] target_total pulses=" << total_pulses
                      << " detected=" << total_detected
                      << " missed=" << (total_pulses - total_detected)
                      << " status="
                      << (total_pulses == 0
                              ? "SKIPPED"
                              : (total_pulses == total_detected
                                     ? "PASS"
                                     : "FAIL"))
                      << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[sink] error=" << error.what() << '\n';
        return 1;
    }
}
