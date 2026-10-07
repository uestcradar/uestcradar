#pragma once

#include "raw_frame.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

inline constexpr char kUpstreamBufName[] = "/uestcradar_upstream";
inline constexpr char kDownstreamBufName[] = "/uestcradar_downstream";
inline constexpr std::uint32_t kRingMagic = 0x52494E47;
inline constexpr std::uint16_t kRingAbiVersion = 6;
inline constexpr std::size_t kRingHeaderSize = 4096;
inline constexpr std::size_t kSlotHeaderSize = 64;
inline constexpr std::uint32_t kDefaultSlotCount = 8;
inline constexpr std::uint32_t kDefaultMaxPayloadBytes = 1024 * 1024;

struct RingBufferConfig {
    std::uint32_t slot_count{kDefaultSlotCount};
    std::uint32_t max_payload_bytes{kDefaultMaxPayloadBytes};
    std::uint64_t type_id{1};
    std::uint32_t type_version{1};
};

// Shared-memory ABI. Monotonic positions count complete records, not bytes.
struct alignas(kRingHeaderSize) RingBufferHeader {
    std::atomic<std::uint32_t> magic{0};
    std::uint16_t abi_version{kRingAbiVersion};
    std::uint16_t header_size{kRingHeaderSize};
    std::uint32_t slot_count{0};
    std::uint32_t slot_header_size{kSlotHeaderSize};
    std::uint64_t slot_stride{0};
    std::uint64_t max_payload_bytes{0};
    std::uint64_t type_id{0};
    std::uint32_t type_version{0};
    std::byte metadata_padding[20]{};

    alignas(64) std::atomic<std::uint64_t> write_position{0};
    std::byte write_padding[56]{};

    alignas(64) std::atomic<std::uint64_t> read_position{0};
    std::byte read_padding[56]{};

    alignas(64) std::atomic<std::uint32_t> shutdown{0};
    std::byte shutdown_padding[60]{};

    std::byte reserved[kRingHeaderSize - 256]{};
};

using RingSlotHeader = uestcradar::Envelope;

struct RingBuffer {
    RingBufferHeader* header{nullptr};
    std::byte* slots{nullptr};
    std::size_t mapping_size{0};
};

enum class RingResult : std::uint8_t {
    ok,
    would_block,
    shutdown,
    invalid_argument,
    corrupt,
};

class RingWriteLease {
public:
    RingWriteLease() noexcept = default;
    RingWriteLease(RingWriteLease&& other) noexcept;
    RingWriteLease& operator=(RingWriteLease&& other) noexcept;
    RingWriteLease(const RingWriteLease&) = delete;
    RingWriteLease& operator=(const RingWriteLease&) = delete;

    [[nodiscard]] std::span<std::byte> payload() const noexcept;
    [[nodiscard]] uestcradar::Envelope& envelope() const noexcept;
    [[nodiscard]] std::span<std::byte> frame_capacity() const noexcept;
    [[nodiscard]] bool active() const noexcept;

private:
    void reset() noexcept;

    RingBuffer* ring_{nullptr};
    std::uint64_t position_{0};
    std::byte* payload_{nullptr};
    uestcradar::Envelope* envelope_{nullptr};
    std::size_t capacity_{0};

    friend RingResult ringbuf_reserve(
        RingBuffer*, RingWriteLease&) noexcept;
    friend RingResult ringbuf_commit(
        RingWriteLease&) noexcept;
    friend void ringbuf_cancel(RingWriteLease&) noexcept;
};

class RingReadLease {
public:
    RingReadLease() noexcept = default;
    RingReadLease(RingReadLease&& other) noexcept;
    RingReadLease& operator=(RingReadLease&& other) noexcept;
    RingReadLease(const RingReadLease&) = delete;
    RingReadLease& operator=(const RingReadLease&) = delete;

    [[nodiscard]] std::span<const std::byte> payload() const noexcept;
    [[nodiscard]] const uestcradar::Envelope& envelope() const noexcept;
    [[nodiscard]] std::span<const std::byte> frame() const noexcept;
    [[nodiscard]] bool active() const noexcept;

private:
    void reset() noexcept;

    RingBuffer* ring_{nullptr};
    std::uint64_t position_{0};
    const std::byte* payload_{nullptr};
    const uestcradar::Envelope* envelope_{nullptr};
    std::size_t length_{0};

    friend RingResult ringbuf_acquire(
        RingBuffer*, RingReadLease&) noexcept;
    friend RingResult ringbuf_release(RingReadLease&) noexcept;
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(sizeof(RingBufferHeader) == kRingHeaderSize);
static_assert(sizeof(RingSlotHeader) == kSlotHeaderSize);
static_assert(offsetof(RingBufferHeader, write_position) == 64);
static_assert(offsetof(RingBufferHeader, read_position) == 128);
static_assert(offsetof(RingBufferHeader, shutdown) == 192);

[[nodiscard]] RingBuffer* ringbuf_create(
    const char* name,
    const RingBufferConfig& config);
[[nodiscard]] RingBuffer* ringbuf_open(const char* name);

[[nodiscard]] RingResult ringbuf_reserve(
    RingBuffer* ring,
    RingWriteLease& lease) noexcept;
[[nodiscard]] RingResult ringbuf_commit(
    RingWriteLease& lease) noexcept;
void ringbuf_cancel(RingWriteLease& lease) noexcept;

[[nodiscard]] RingResult ringbuf_acquire(
    RingBuffer* ring,
    RingReadLease& lease) noexcept;
[[nodiscard]] RingResult ringbuf_release(
    RingReadLease& lease) noexcept;

[[nodiscard]] std::uint32_t ringbuf_slot_count(
    const RingBuffer* ring) noexcept;
[[nodiscard]] std::size_t ringbuf_max_payload_bytes(
    const RingBuffer* ring) noexcept;
[[nodiscard]] std::size_t ringbuf_occupied_slots(
    const RingBuffer* ring) noexcept;
[[nodiscard]] std::span<std::byte> ringbuf_storage(
    RingBuffer* ring) noexcept;
[[nodiscard]] bool ringbuf_is_shutdown(
    const RingBuffer* ring) noexcept;
void ringbuf_shutdown(RingBuffer* ring) noexcept;
void ringbuf_close(RingBuffer* ring) noexcept;
void ringbuf_unlink(const char* name) noexcept;
