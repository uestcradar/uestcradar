#include "my_rd_algorithm.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function>
void require_throws(Function&& function, const char* message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

uestcradar::PulseCompressionMetadata test_metadata(
    std::uint32_t pulse_index,
    std::uint32_t range_bins) {
    return {
        .channel_count = radar_qt_example::kChannelCount,
        .range_bin_count = range_bins,
        .pulse_index = pulse_index,
        .pulses_per_cpi = radar_qt_example::kPulsesPerCpi,
        .range_resolution_m = radar_qt_example::kRangeResolutionM,
    };
}

void test_contract_sizes() {
    require(radar_qt_example::kPulseFrameBytes == 867024,
            "PulseCompressionFrame byte count changed");
    require(radar_qt_example::kRdFrameBytes == 28177532,
            "RDFrame byte count changed");
    require(radar_qt_example::kRdFrameBytes <=
                radar_qt_example::kMaxFrameBytes,
            "RDFrame exceeds 32 MiB");
}

void test_sequence_validation() {
    constexpr std::uint32_t range_bins = 4;
    std::vector<uestcradar::ComplexFloat32> samples(range_bins);
    radar_qt_example::CpiBuffer starts_late(range_bins);
    require_throws(
        [&] { starts_late.push(test_metadata(1, range_bins), samples); },
        "a CPI starting at pulse 1 was accepted");

    radar_qt_example::CpiBuffer missing(range_bins);
    missing.push(test_metadata(0, range_bins), samples);
    require_throws(
        [&] { missing.push(test_metadata(2, range_bins), samples); },
        "a CPI with a missing pulse was accepted");

    radar_qt_example::CpiBuffer duplicate(range_bins);
    duplicate.push(test_metadata(0, range_bins), samples);
    require_throws(
        [&] { duplicate.push(test_metadata(0, range_bins), samples); },
        "a CPI with a duplicate pulse was accepted");
}

void test_placeholder_algorithm() {
    constexpr std::uint32_t range_bins = 8;
    radar_qt_example::CpiBuffer cpi(range_bins);
    for (std::uint32_t pulse = 0;
         pulse < radar_qt_example::kPulsesPerCpi;
         ++pulse) {
        std::vector<uestcradar::ComplexFloat32> samples(range_bins);
        for (std::uint32_t range = 0; range < range_bins; ++range) {
            samples[range] = {
                static_cast<float>(pulse + 1),
                static_cast<float>(range + 1),
            };
        }
        cpi.push(test_metadata(pulse, range_bins), samples);
    }
    require(cpi.ready(), "64 standard frames did not complete a CPI");
    std::vector<float> rd(
        static_cast<std::size_t>(range_bins) *
        radar_qt_example::kDopplerBinCount);
    const auto result = radar_qt_example::compute_rd(
        cpi,
        {rd.data(), range_bins, radar_qt_example::kDopplerBinCount});
    require(rd.front() == std::hypot(1.0F, 1.0F),
            "placeholder did not map the first sample");
    require(rd[64] == std::hypot(1.0F, 1.0F),
            "65th Doppler bin did not wrap to pulse zero");
    require(result.peak_range_bin == range_bins - 1,
            "placeholder peak result is invalid");
}

}  // namespace

int main() {
    try {
        test_contract_sizes();
        test_sequence_validation();
        test_placeholder_algorithm();
        std::cout << "rd-algorithm-test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "rd-algorithm-test: FAIL " << error.what() << '\n';
        return 1;
    }
}
