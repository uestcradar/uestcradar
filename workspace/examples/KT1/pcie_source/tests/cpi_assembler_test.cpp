#include "cpi_assembler.hpp"
#include <cassert>

int main() {
    using namespace pcie_source;
    Templates templates{};
    for (unsigned t = 0; t < 10; ++t) {
        templates[t].channel_count = 1;
        templates[t].samples_per_channel = samples_per_cpi;
        templates[t].pulse_count = 64;
        templates[t].pulse_phase_rad[0] = t;
    }
    CpiAssembler assembler(templates);
    const auto sample = [](std::size_t i) {
        return uestcradar::ComplexInt16{static_cast<std::int16_t>(i % 30000),
            static_cast<std::int16_t>(-static_cast<int>(i % 29000))};
    };
    std::uint64_t frames = 0;
    auto emit = [&](Cpi frame) {
        assert(frame.metadata.cpi_index == frames);
        assert(frame.metadata.pulse_phase_rad[0] == frames % 10);
        assert(frame.samples.size() == samples_per_cpi);
        for (std::size_t i = 0; i < frame.samples.size(); ++i) {
            const auto expected = sample(frames * samples_per_cpi + i);
            assert(frame.samples[i].i == expected.i && frame.samples[i].q == expected.q);
        }
        ++frames;
    };
    // Odd packet sizes cross CPI boundaries; one packet can contain several CPIs.
    for (std::size_t start = 0; start < samples_per_cpi * 11;) {
        const auto n = std::min(samples_per_cpi * 2 + 7, samples_per_cpi * 11 - start);
        std::vector<uestcradar::ComplexInt16> packet(n);
        for (std::size_t i = 0; i < n; ++i) packet[i] = sample(start + i);
        assembler.append(packet, emit);
        start += n;
    }
    assert(frames == 11);
    std::vector<uestcradar::ComplexInt16> partial(19, {99, 99});
    assembler.append(partial, emit);
    assert(assembler.discard_partial() == 19);
    std::vector<uestcradar::ComplexInt16> next(samples_per_cpi, {7, -8});
    assembler.append(next, [&](Cpi frame) {
        assert(frame.metadata.cpi_index == 11);
        for (const auto v : frame.samples) assert(v.i == 7 && v.q == -8);
    });
    assert(assembler.completed() == 12);
    assert(assembler.discard_partial() == 0);
}
