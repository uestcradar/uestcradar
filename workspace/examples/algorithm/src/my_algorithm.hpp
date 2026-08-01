#pragma once

#include <data.h>

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace radar_algorithm {

struct CompressionResult {
    std::size_t peak_range_bin{};
    float peak_magnitude{};
};

inline CompressionResult pulse_compress(
    uestcradar::Array2D<uestcradar::ComplexInt16> iq,
    uestcradar::Array2D<uestcradar::ComplexFloat32> pulse) {
    if (iq.rows() != pulse.rows() ||
        iq.columns() != pulse.columns()) {
        throw std::invalid_argument("IQ and pulse shapes do not match");
    }

    CompressionResult result;
    constexpr float scale = 1.0F / 400'000'000.0F;

    for (std::size_t channel = 0; channel < iq.rows(); ++channel) {
        for (std::size_t lag = 0; lag < iq.columns(); ++lag) {
            float real = 0.0F;
            float imag = 0.0F;
            for (std::size_t sample = lag;
                 sample < iq.columns();
                 ++sample) {
                const auto value = iq[channel][sample];
                const auto reference = iq[channel][sample - lag];
                real += static_cast<float>(value.i) * reference.i +
                        static_cast<float>(value.q) * reference.q;
                imag += static_cast<float>(value.q) * reference.i -
                        static_cast<float>(value.i) * reference.q;
            }
            pulse[channel][lag] = {real * scale, imag * scale};

            const float magnitude = std::hypot(real, imag) * scale;
            if (magnitude > result.peak_magnitude) {
                result = {lag, magnitude};
            }
        }
    }
    return result;
}

}  // namespace radar_algorithm
