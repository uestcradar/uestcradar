#include "my_rd_algorithm.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
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
    std::uint32_t range_bins,
    double range_resolution_m = 4.8828125) {
    return {
        .channel_count = radar_qt_example::kChannelCount,
        .range_bin_count = range_bins,
        .pulse_index = pulse_index,
        .pulses_per_cpi = radar_qt_example::kPulsesPerCpi,
        .range_resolution_m = range_resolution_m,
    };
}

void test_contract_sizes() {
    require(radar_qt_example::rd_frame_bytes(22196) == 5770992,
            "current real-data RDFrame byte count is invalid");
    require(radar_qt_example::rd_frame_bytes(33293) == 8656212,
            "extended-window RDFrame byte count is invalid");
    require(radar_qt_example::rd_frame_bytes(
                radar_qt_example::kMaxRangeBinCount) <=
            radar_qt_example::kMaxFrameBytes,
            "maximum RDFrame exceeds 32 MiB");
}

void test_sequence_resynchronization() {
    constexpr std::uint32_t range_bins = 4;
    std::vector<uestcradar::ComplexFloat32> samples(range_bins);
    radar_qt_example::CpiBuffer starts_late;
    starts_late.push(test_metadata(1, range_bins), samples);
    require(!starts_late.ready() && starts_late.range_bin_count() == 0,
            "a partial CPI was not ignored");

    radar_qt_example::CpiBuffer missing;
    missing.push(test_metadata(0, range_bins), samples);
    missing.push(test_metadata(2, range_bins), samples);
    require(missing.range_bin_count() == 0,
            "an incomplete CPI was not discarded");
    missing.push(test_metadata(0, range_bins, 2.5), samples);
    for (std::uint32_t pulse = 1;
         pulse < radar_qt_example::kPulsesPerCpi;
         ++pulse) {
        missing.push(test_metadata(pulse, range_bins, 2.5), samples);
    }
    require(missing.ready() && missing.range_resolution_m() == 2.5,
            "CPI did not recover at the next pulse zero");

    auto invalid = test_metadata(0, range_bins);
    invalid.range_bin_count = radar_qt_example::kMaxRangeBinCount + 1;
    require_throws(
        [&] { missing.push(invalid, samples); },
        "an oversized RD input shape was accepted");
}

void test_placeholder_algorithm() {
    constexpr std::uint32_t range_bins = 8;
    radar_qt_example::CpiBuffer cpi;
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

    const auto output = std::filesystem::current_path() /
        "rd-algorithm-test-output.pgm";
    std::filesystem::remove(output);
    radar_qt_example::save_rd_map_pgm(
        {rd.data(), range_bins, radar_qt_example::kDopplerBinCount}, output);
    std::ifstream image(output, std::ios::binary);
    std::string magic;
    std::size_t width = 0;
    std::size_t height = 0;
    int maximum = 0;
    image >> magic >> width >> height >> maximum;
    require(magic == "P5" &&
                width == radar_qt_example::kDopplerBinCount &&
                height == range_bins && maximum == 255,
            "RDMap PGM dimensions are invalid");
    image.close();
    std::filesystem::remove(output);
}

}  // namespace

int main() {
    try {
        test_contract_sizes();
        test_sequence_resynchronization();
        test_placeholder_algorithm();
        std::cout << "rd-algorithm-test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "rd-algorithm-test: FAIL " << error.what() << '\n';
        return 1;
    }
}
