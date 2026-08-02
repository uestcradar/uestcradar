#include "my_rd_algorithm.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <vector>

int main() {
    constexpr std::size_t range_bins = 8;
    constexpr std::size_t target_range = 3;
    constexpr std::size_t target_doppler = 2;
    radar_qt_example::CpiBuffer cpi;

    for (std::size_t pulse = 0;
         pulse < radar_qt_example::kPulsesPerCpi;
         ++pulse) {
        std::vector<uestcradar::ComplexFloat32> bins(range_bins);
        const float phase =
            2.0F * std::numbers::pi_v<float> *
            static_cast<float>(target_doppler * pulse) /
            static_cast<float>(radar_qt_example::kPulsesPerCpi);
        bins[target_range] = {100.0F * std::cos(phase),
                              100.0F * std::sin(phase)};
        cpi.push(
            static_cast<std::uint32_t>(pulse),
            radar_qt_example::kPulsesPerCpi,
            bins);
    }

    std::vector<float> rd(
        range_bins * radar_qt_example::kPulsesPerCpi);
    const auto result = radar_qt_example::compute_rd(
        cpi,
        {rd.data(), range_bins, radar_qt_example::kPulsesPerCpi});
    if (result.peak_range_bin != target_range ||
        result.peak_doppler_bin != target_doppler) {
        std::cerr << "rd-algorithm-test: unexpected RD peak\n";
        return 1;
    }
    return 0;
}
