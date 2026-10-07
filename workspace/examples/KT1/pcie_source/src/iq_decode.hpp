#pragma once

#include <data.h>
#include <array>
#include <bit>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace pcie_source {

// From cycore/device/pcie_device_handle.cpp::reassemble_pcie_iq_packet.
inline constexpr std::array<std::array<unsigned, 2>, 4> pipe_channels{{
    {{0, 1}}, {{4, 5}}, {{2, 6}}, {{3, 7}}
}};

struct PacketView {
    std::array<std::span<const std::byte>, 4> pipes;
    unsigned mask{};
    unsigned group{};
    bool control{};
};

inline std::vector<uestcradar::ComplexInt16> decode_channel(
    const PacketView& packet, unsigned channel) {
    if (channel >= 8) throw std::invalid_argument("channel must be 0..7");
    if (packet.control || packet.group == 15) return {};
    if (packet.group != 0 || (packet.mask & 15) != 15)
        throw std::invalid_argument("incomplete synchronous IQ packet");
    const auto bytes = packet.pipes[0].size();
    if (bytes == 0 || bytes % 8 != 0)
        throw std::invalid_argument("pipe length must be a positive multiple of 8");
    for (const auto pipe : packet.pipes) {
        if (pipe.data() == nullptr || pipe.size() != bytes)
            throw std::invalid_argument("missing pipe or unequal pipe lengths");
    }
    unsigned selected_pipe = 0, lane = 0;
    for (unsigned p = 0; p < 4; ++p)
        for (unsigned l = 0; l < 2; ++l)
            if (pipe_channels[p][l] == channel) { selected_pipe = p; lane = l; }
    std::vector<uestcradar::ComplexInt16> result(bytes / 8);
    // Decode from a stable host snapshot, with unaligned buffers and little-endian IQ.
    const auto* source = packet.pipes[selected_pipe].data();
    const auto le16 = [](const std::byte* p) {
        const auto bits = static_cast<std::uint16_t>(
            std::to_integer<unsigned>(p[0]) | (std::to_integer<unsigned>(p[1]) << 8));
        return std::bit_cast<std::int16_t>(bits);
    };
    for (std::size_t t = 0; t < result.size(); ++t) {
        const auto* sample = source + t * 8 + lane * 4;
        result[t] = {le16(sample), le16(sample + 2)};
    }
    return result;
}

} // namespace pcie_source
