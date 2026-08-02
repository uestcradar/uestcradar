#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace uestcradar {

inline constexpr std::size_t kEnvelopeSize = 64;

struct alignas(kEnvelopeSize) Envelope {
    std::uint64_t frame_id{};
    std::uint64_t timestamp{};
    std::uint64_t type_id{};
    std::uint32_t type_version{};
    std::uint32_t payload_length{};
    std::uint32_t flags{};
    std::byte reserved[28]{};
};

static_assert(sizeof(Envelope) == kEnvelopeSize);
static_assert(alignof(Envelope) == kEnvelopeSize);
static_assert(std::is_standard_layout_v<Envelope>);
static_assert(std::is_trivially_copyable_v<Envelope>);
static_assert(offsetof(Envelope, frame_id) == 0);
static_assert(offsetof(Envelope, timestamp) == 8);
static_assert(offsetof(Envelope, type_id) == 16);
static_assert(offsetof(Envelope, type_version) == 24);
static_assert(offsetof(Envelope, payload_length) == 28);
static_assert(offsetof(Envelope, flags) == 32);
static_assert(offsetof(Envelope, reserved) == 36);

}  // namespace uestcradar
