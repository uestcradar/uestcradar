#pragma once

#include <csignal>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sidecar::telemetry {

enum class LinkDirection {
    ingress,
    egress,
};

enum class ConnectionState {
    disabled,
    disconnected,
    connected,
};

enum class TransportKind {
    tcp,
    rdma,
};

struct RingSnapshot {
    std::uint32_t capacity_slots{};
    std::uint32_t used_slots{};
    std::uint64_t write_position{};
    std::uint64_t read_position{};
    bool shutdown{};
};

struct LinkSnapshot {
    ConnectionState connection{ConnectionState::disconnected};
    std::uint64_t payload_bytes_total{};
    RingSnapshot ring;
};

using SnapshotCallback = std::function<bool(LinkSnapshot&)>;

struct TelemetryTarget {
    std::string link_id;
    std::string peer_node_id;
    LinkDirection direction;
    TransportKind transport;
    SnapshotCallback fetch_snapshot;
};

[[nodiscard]] int run_telemetry_exporter(
    volatile std::sig_atomic_t& running,
    const std::vector<TelemetryTarget>& targets);

}  // namespace sidecar::telemetry
