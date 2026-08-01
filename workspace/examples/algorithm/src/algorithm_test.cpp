#include "my_algorithm.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    constexpr std::size_t samples = 16;
    std::vector<uestcradar::ComplexInt16> iq(samples);
    std::vector<uestcradar::ComplexFloat32> pulse(samples);
    for (std::size_t index = 0; index < samples; ++index) {
        iq[index] = {
            static_cast<std::int16_t>(1000 + index * 10),
            static_cast<std::int16_t>(500 - index * 5),
        };
    }

    const auto result = radar_algorithm::pulse_compress(
        {iq.data(), 1, samples},
        {pulse.data(), 1, samples});
    if (result.peak_range_bin != 0 ||
        !std::isfinite(result.peak_magnitude)) {
        std::cerr << "algorithm-test: unexpected compression peak\n";
        return 1;
    }
    return 0;
}
