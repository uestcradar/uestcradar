#include "my_waveform.hpp"

#include <cassert>
#include <cstddef>

int main() {
    constexpr std::size_t samples = 4096;
    const auto channel_zero = radar_example::iq_sample(0, samples / 5, samples);
    const auto channel_one_same_position =
        radar_example::iq_sample(1, samples / 5, samples);
    const auto channel_one_peak =
        radar_example::iq_sample(1, samples * 2 / 5, samples);

    assert(channel_zero.i >= 18'000);
    assert(channel_one_same_position.i != channel_zero.i);
    assert(channel_one_peak.i > channel_zero.i);
    assert(channel_one_peak.q < -20'000);
    return 0;
}
