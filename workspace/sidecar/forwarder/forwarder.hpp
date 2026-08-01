#pragma once

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>

struct RingBuffer;

namespace sidecar::network {
class UCXMemoryRegion;
class UCXTransport;
}  // namespace sidecar::network

namespace sidecar::forwarder {

struct LegMetrics {
    alignas(64) std::atomic<std::uint64_t> payload_bytes_total{0};
    alignas(64) std::atomic<bool> connected{false};
};

struct DroppedFrames {
    std::size_t frames{0};
    std::size_t bytes{0};
};

void run_ingress_session(
    volatile std::sig_atomic_t& running,
    RingBuffer* input,
    network::UCXTransport& transport,
    const network::UCXMemoryRegion& input_memory,
    LegMetrics& metrics);

void run_egress_session(
    volatile std::sig_atomic_t& running,
    RingBuffer* output,
    network::UCXTransport& transport,
    const network::UCXMemoryRegion& output_memory,
    LegMetrics& metrics);

[[nodiscard]] DroppedFrames drop_stale_frames(
    RingBuffer* output) noexcept;

}  // namespace sidecar::forwarder
