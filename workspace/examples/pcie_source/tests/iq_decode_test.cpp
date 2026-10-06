#include "iq_decode.hpp"
#include <cassert>

int main() {
    using namespace pcie_source;
    std::array<std::array<uestcradar::ComplexInt16, 6>, 4> input{{
        {{{0, -1}, {10, -11}, {1, -2}, {11, -12}, {2, -3}, {12, -13}}},
        {{{40, -41}, {50, -51}, {41, -42}, {51, -52}, {42, -43}, {52, -53}}},
        {{{20, -21}, {60, -61}, {21, -22}, {61, -62}, {22, -23}, {62, -63}}},
        {{{30, -31}, {70, -71}, {31, -32}, {71, -72}, {32, -33}, {72, -73}}}
    }};
    static_assert(std::endian::native == std::endian::little);
    PacketView packet{};
    packet.mask = 15;
    for (unsigned p = 0; p < 4; ++p) packet.pipes[p] = std::as_bytes(std::span(input[p]));
    for (unsigned channel = 0; channel < 8; ++channel) {
        const auto result = decode_channel(packet, channel);
        assert(result.size() == 3);
        for (unsigned t = 0; t < 3; ++t) {
            assert(result[t].i == static_cast<int>(channel * 10 + t));
            assert(result[t].q == -static_cast<int>(channel * 10 + t + 1));
        }
    }
    // Odd-sized, unaligned pipes exercise scalar tails and signed CS16 limits.
    std::array<std::array<std::byte, 33 * 8 + 1>, 4> raw{};
    PacketView unaligned{};
    unaligned.mask = 15;
    for (unsigned p = 0; p < 4; ++p) {
        for (unsigned t = 0; t < 33; ++t)
            for (unsigned lane = 0; lane < 2; ++lane) {
                const std::uint16_t i = 0x8000 + p * 100 + t * 2 + lane;
                const std::uint16_t q = 0x7fff - p * 100 - t * 2 - lane;
                auto* sample = raw[p].data() + 1 + t * 8 + lane * 4;
                sample[0] = std::byte(i & 255); sample[1] = std::byte(i >> 8);
                sample[2] = std::byte(q & 255); sample[3] = std::byte(q >> 8);
            }
        unaligned.pipes[p] = std::span(raw[p]).subspan(1);
    }
    constexpr unsigned expected_pipe[]{0, 0, 2, 3, 1, 1, 2, 3};
    constexpr unsigned expected_lane[]{0, 1, 0, 0, 0, 1, 1, 1};
    for (unsigned channel = 0; channel < 8; ++channel) {
        const auto result = decode_channel(unaligned, channel);
        assert(result.size() == 33);
        for (int t = 0; t < 33; ++t) {
            const int offset = expected_pipe[channel] * 100 + t * 2 + expected_lane[channel];
            assert(result[t].i == -32768 + offset && result[t].q == 32767 - offset);
        }
    }
    auto rejected = [&](PacketView value, unsigned channel = 0) {
        try { (void)decode_channel(value, channel); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    assert(rejected(packet, 8));
    auto bad = packet; bad.mask = 7; assert(rejected(bad));
    bad = packet; bad.pipes[2] = {}; assert(rejected(bad));
    bad = packet; bad.pipes[0] = bad.pipes[0].first(7); assert(rejected(bad));
    bad = packet; bad.group = 9; assert(rejected(bad));
    bad = packet; bad.control = true; assert(decode_channel(bad, 0).empty());
    bad = packet; bad.group = 15; assert(decode_channel(bad, 0).empty());
}
