#pragma once
#include <data.h>
#include <bit>
#include <cstdint>
#include <vector>

namespace raw_iq_fixture {
// Synthetic integers only; no hardware time-unit claim.
inline uestcradar::RawIQMetadata metadata(std::uint64_t index, std::uint32_t samples) {
    return {index == 0 ? UINT64_MAX : UINT64_C(0xfedcba9800000000) + index * 32,
            UINT64_C(0x8000000000000000) + index * 98304, 1, samples};
}
inline std::vector<uestcradar::ComplexInt16> samples(std::uint32_t count) {
    std::vector<uestcradar::ComplexInt16> result(count);
    std::uint32_t random = 0x12345678;
    for (auto& sample : result) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        sample = {std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(random)),
                  std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(random >> 16))};
    }
    return result;
}
inline void mark(std::span<uestcradar::ComplexInt16> data, std::uint64_t index) {
    data.front() = {std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(index)),
                   std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(index >> 16))};
}
} // namespace raw_iq_fixture
