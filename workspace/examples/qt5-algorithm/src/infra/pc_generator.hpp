#pragma once

#include "../rd_contract.hpp"

#include <data.h>

#include <cstdint>
#include <random>
#include <span>

namespace radar_qt_example {

inline uestcradar::PulseCompressionMetadata describe_pulse(
    std::uint32_t pulse_index) {
    return {
        .channel_count = kChannelCount,
        .range_bin_count = kRangeBinCount,
        .pulse_index = pulse_index,
        .pulses_per_cpi = kPulsesPerCpi,
        .range_resolution_m = kRangeResolutionM,
    };
}

class RandomPulseGenerator {
public:
    explicit RandomPulseGenerator(std::uint64_t seed)
        : engine_(seed), distribution_(-1.0F, 1.0F) {}

    void fill(std::span<uestcradar::ComplexFloat32> samples) {
        for (auto& sample : samples) {
            sample = {distribution_(engine_), distribution_(engine_)};
        }
    }

private:
    std::mt19937_64 engine_;
    std::uniform_real_distribution<float> distribution_;
};

}  // namespace radar_qt_example
