#pragma once
#include <data.h>
#include <bit>
#include <cstdint>
#include <span>
#include <vector>

namespace raw_iq {
static_assert(std::endian::native == std::endian::little,
              "CS16 payload copying requires a little-endian host");

struct Block {
    uestcradar::RawIQMetadata metadata;
    std::vector<uestcradar::ComplexInt16> samples;
};

// Test/diagnostic fingerprint, NOT authentication or a hardware continuity proof.
// Canonical input: TX(u64 LE), RX(u64 LE), rows(u32 LE), columns(u32 LE), CS16 bytes.
struct Digest {
    std::uint64_t value{UINT64_C(14695981039346656037)};
    std::uint64_t frames{}, samples{};
    void bytes(std::span<const std::byte> data) {
        for (const auto byte : data) value = (value ^ std::to_integer<unsigned char>(byte)) * UINT64_C(1099511628211);
    }
    template<class T> void integer(T number) {
        for (unsigned n = 0; n < sizeof(T); ++n) {
            const auto byte = std::byte((number >> (8 * n)) & 255);
            bytes({&byte, 1});
        }
    }
    void add(const uestcradar::RawIQMetadata& metadata, std::span<const uestcradar::ComplexInt16> data) {
        integer(metadata.tx_timestamp);
        integer(metadata.rx_timestamp);
        integer(metadata.channel_count);
        integer(metadata.samples_per_channel);
        bytes(std::as_bytes(data));
        ++frames;
        samples += data.size();
    }
};
} // namespace raw_iq
