#include "forwarder/forwarder.hpp"
#include "network/ucx_transport.hpp"
#include "ringbuf/ringbuf.hpp"
#include "telemetry/telemetry.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t running = 1;
constexpr std::uint64_t kDefaultFrameTypeId = 1;
constexpr std::chrono::milliseconds kRetryDelay{250};

void stop(int) {
    running = 0;
}

void install_signal_handlers() {
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
}

std::size_t size_from_environment(
    const char* name,
    std::size_t fallback,
    std::size_t minimum,
    std::size_t maximum) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        throw std::invalid_argument(
            std::string{name} + " is out of range");
    }
    return static_cast<std::size_t>(parsed);
}

std::string environment_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value == nullptr || value[0] == '\0'
               ? std::string{fallback}
               : std::string{value};
}

std::uint64_t uint64_from_environment(
    const char* name,
    std::uint64_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(value, &end, 0);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0) {
        throw std::invalid_argument(
            std::string{name} + " must be a positive integer");
    }
    return static_cast<std::uint64_t>(parsed);
}

RingBufferConfig ring_config_from_environment(
    const char* slot_count_name,
    const char* max_payload_name,
    const char* type_id_name,
    const char* type_version_name) {
    return {
        static_cast<std::uint32_t>(size_from_environment(
            slot_count_name, kDefaultSlotCount, 2, UINT32_MAX)),
        static_cast<std::uint32_t>(size_from_environment(
            max_payload_name,
            kDefaultMaxPayloadBytes,
            1,
            INT32_MAX)),
        uint64_from_environment(type_id_name, kDefaultFrameTypeId),
        static_cast<std::uint32_t>(size_from_environment(
            type_version_name, 2, 1, UINT32_MAX)),
    };
}

enum class LegRole {
    disabled,
    listen,
    connect,
};

struct LegConfig {
    std::string name;
    LegRole role;
    std::string bind_host;
    std::string peer_host;
    std::string peer_node_id;
    std::uint16_t port;
    std::chrono::milliseconds connect_timeout;
    sidecar::network::DataPathMode data_path;
};

LegRole parse_role(const std::string& value, const std::string& name) {
    if (value == "disabled") {
        return LegRole::disabled;
    }
    if (value == "listen") {
        return LegRole::listen;
    }
    if (value == "connect") {
        return LegRole::connect;
    }
    throw std::invalid_argument(
        name + " must be disabled, listen, or connect");
}

sidecar::network::DataPathMode parse_data_path(
    const std::string& value,
    const std::string& name) {
    if (value == "functional") {
        return sidecar::network::DataPathMode::functional;
    }
    if (value == "strict-rdma") {
        return sidecar::network::DataPathMode::strict_rdma;
    }
    throw std::invalid_argument(
        name + " must be functional or strict-rdma");
}

LegConfig leg_config_from_environment(
    const char* display_name,
    const char* prefix,
    const char* default_role,
    std::uint16_t default_port) {
    const std::string base = std::string{"SIDECAR_"} + prefix;
    const std::string role_name = base + "_ROLE";
    const std::string bind_name = base + "_BIND_HOST";
    const std::string peer_name = base + "_PEER_HOST";
    const std::string peer_node_name = base + "_PEER_NODE_ID";
    const std::string port_name = base + "_PORT";
    const std::string timeout_name = base + "_CONNECT_TIMEOUT_MS";
    const std::string path_name = base + "_DATA_PATH";

    return {
        display_name,
        parse_role(
            environment_or(role_name.c_str(), default_role), role_name),
        environment_or(bind_name.c_str(), "0.0.0.0"),
        environment_or(peer_name.c_str(), "127.0.0.1"),
        environment_or(peer_node_name.c_str(), ""),
        static_cast<std::uint16_t>(size_from_environment(
            port_name.c_str(), default_port, 1, 65535)),
        std::chrono::milliseconds{size_from_environment(
            timeout_name.c_str(),
            2'000,
            1,
            std::numeric_limits<std::uint32_t>::max())},
        parse_data_path(
            environment_or(path_name.c_str(), "functional"), path_name),
    };
}

void reject_legacy_configuration() {
    constexpr const char* legacy_names[] = {
        "SIDECAR_UCX_ROLE",
        "SIDECAR_UCX_BIND_HOST",
        "SIDECAR_UCX_PEER_HOST",
        "SIDECAR_UCX_PORT",
        "SIDECAR_UCX_CONNECT_TIMEOUT_MS",
        "SIDECAR_UCX_DATA_PATH",
    };
    for (const char* name : legacy_names) {
        if (std::getenv(name) != nullptr) {
            throw std::invalid_argument(
                std::string{name} +
                " is obsolete; configure SIDECAR_UPSTREAM_* and "
                "SIDECAR_DOWNSTREAM_* legs");
        }
    }
}

sidecar::network::UCXTransport create_transport_once(
    const LegConfig& config) {
    if (config.role == LegRole::listen) {
        return sidecar::network::UCXTransport::accept_one(
            sidecar::network::EndpointOptions{
                config.bind_host,
                config.port,
                config.connect_timeout,
                config.data_path,
            });
    }
    if (config.role == LegRole::connect) {
        return sidecar::network::UCXTransport::connect(
            sidecar::network::EndpointOptions{
                config.peer_host,
                config.port,
                config.connect_timeout,
                config.data_path,
            });
    }
    throw std::logic_error("cannot create a disabled leg transport");
}

void retry_delay() {
    const auto deadline = std::chrono::steady_clock::now() + kRetryDelay;
    while (running != 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
}

class OwnedRing {
public:
    OwnedRing(std::string name, const RingBufferConfig& config)
        : name_(std::move(name)),
          ring_(ringbuf_create(name_.c_str(), config)) {}

    OwnedRing(const OwnedRing&) = delete;
    OwnedRing& operator=(const OwnedRing&) = delete;

    ~OwnedRing() {
        ringbuf_shutdown(ring_);
        ringbuf_close(ring_);
        ringbuf_unlink(name_.c_str());
    }

    [[nodiscard]] RingBuffer* get() const noexcept {
        return ring_;
    }

private:
    std::string name_;
    RingBuffer* ring_;
};

bool snapshot_ring(
    const RingBuffer* ring,
    sidecar::telemetry::RingSnapshot& output) noexcept {
    const std::uint64_t slot_count = ring->header->slot_count;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::uint64_t read = ring->header->read_position.load(
            std::memory_order_acquire);
        const std::uint64_t write = ring->header->write_position.load(
            std::memory_order_acquire);
        if (write >= read && write - read <= slot_count) {
            output = sidecar::telemetry::RingSnapshot{
                static_cast<std::uint32_t>(slot_count),
                static_cast<std::uint32_t>(write - read),
                write,
                read,
                ring->header->shutdown.load(
                    std::memory_order_acquire) != 0,
            };
            return true;
        }
    }
    return false;
}

sidecar::telemetry::TransportKind telemetry_transport(
    const LegConfig& config) noexcept {
    return config.data_path == sidecar::network::DataPathMode::strict_rdma
               ? sidecar::telemetry::TransportKind::rdma
               : sidecar::telemetry::TransportKind::tcp;
}

bool snapshot_link(
    const LegConfig& config,
    const RingBuffer* ring,
    const sidecar::forwarder::LegMetrics& metrics,
    sidecar::telemetry::LinkSnapshot& output) noexcept {
    if (!snapshot_ring(ring, output.ring)) {
        return false;
    }
    if (config.role == LegRole::disabled) {
        output.connection = sidecar::telemetry::ConnectionState::disabled;
    } else if (metrics.connected.load(std::memory_order_acquire)) {
        output.connection = sidecar::telemetry::ConnectionState::connected;
    } else {
        output.connection = sidecar::telemetry::ConnectionState::disconnected;
    }
    output.payload_bytes_total =
        metrics.payload_bytes_total.load(std::memory_order_relaxed);
    return true;
}

void log_dropped(
    const LegConfig& config,
    const sidecar::forwarder::DroppedFrames& dropped) {
    if (dropped.frames == 0) {
        return;
    }
    std::cerr << "sidecar: leg=" << config.name
              << " event=stale_frames_dropped frames=" << dropped.frames
              << " bytes=" << dropped.bytes << std::endl;
}

void run_ingress_leg(
    const LegConfig& config,
    RingBuffer* input,
    sidecar::forwarder::LegMetrics& metrics) {
    while (running != 0 && !ringbuf_is_shutdown(input)) {
        try {
            sidecar::network::UCXTransport transport =
                create_transport_once(config);
            sidecar::network::UCXMemoryRegion memory =
                transport.register_memory(ringbuf_storage(input));
            std::cout << "sidecar: leg=" << config.name
                      << " event=connected" << std::endl;
            sidecar::forwarder::run_ingress_session(
                running, input, transport, memory, metrics);
            return;
        } catch (const std::exception& error) {
            if (running == 0 || ringbuf_is_shutdown(input)) {
                return;
            }
            std::cerr << "sidecar: leg=" << config.name
                      << " event=retry error=\"" << error.what()
                      << "\"" << std::endl;
            retry_delay();
        }
    }
}

void run_egress_leg(
    const LegConfig& config,
    RingBuffer* output,
    sidecar::forwarder::LegMetrics& metrics) {
    while (running != 0 && !ringbuf_is_shutdown(output)) {
        log_dropped(
            config, sidecar::forwarder::drop_stale_frames(output));
        try {
            sidecar::network::UCXTransport transport =
                create_transport_once(config);
            log_dropped(
                config, sidecar::forwarder::drop_stale_frames(output));
            sidecar::network::UCXMemoryRegion memory =
                transport.register_memory(ringbuf_storage(output));
            std::cout << "sidecar: leg=" << config.name
                      << " event=connected" << std::endl;
            sidecar::forwarder::run_egress_session(
                running, output, transport, memory, metrics);
            return;
        } catch (const std::exception& error) {
            if (running == 0 || ringbuf_is_shutdown(output)) {
                return;
            }
            std::cerr << "sidecar: leg=" << config.name
                      << " event=retry error=\"" << error.what()
                      << "\"" << std::endl;
            log_dropped(
                config, sidecar::forwarder::drop_stale_frames(output));
            retry_delay();
        }
    }
}

}  // namespace

int main() {
    install_signal_handlers();

    try {
        reject_legacy_configuration();
        const LegConfig upstream_leg = leg_config_from_environment(
            "upstream", "UPSTREAM", "listen", 13337);
        const LegConfig downstream_leg = leg_config_from_environment(
            "downstream", "DOWNSTREAM", "disabled", 13338);
        const RingBufferConfig upstream_config =
            ring_config_from_environment(
                "SIDECAR_UPSTREAM_SLOT_COUNT",
                "SIDECAR_UPSTREAM_MAX_PAYLOAD_BYTES",
                "SIDECAR_UPSTREAM_TYPE_ID",
                "SIDECAR_UPSTREAM_TYPE_VERSION");
        const RingBufferConfig downstream_config =
            ring_config_from_environment(
                "SIDECAR_DOWNSTREAM_SLOT_COUNT",
                "SIDECAR_DOWNSTREAM_MAX_PAYLOAD_BYTES",
                "SIDECAR_DOWNSTREAM_TYPE_ID",
                "SIDECAR_DOWNSTREAM_TYPE_VERSION");
        OwnedRing upstream{
            environment_or(
                "SIDECAR_UPSTREAM_SHM_NAME", kUpstreamBufName),
            upstream_config};
        OwnedRing downstream{
            environment_or(
                "SIDECAR_DOWNSTREAM_SHM_NAME", kDownstreamBufName),
            downstream_config};
        sidecar::forwarder::LegMetrics upstream_metrics;
        sidecar::forwarder::LegMetrics downstream_metrics;

        const std::vector<sidecar::telemetry::TelemetryTarget> targets{
            {
                "upstream",
                upstream_leg.peer_node_id,
                sidecar::telemetry::LinkDirection::ingress,
                telemetry_transport(upstream_leg),
                [&upstream_leg, ring = upstream.get(), &upstream_metrics](
                    sidecar::telemetry::LinkSnapshot& output) noexcept {
                    return snapshot_link(
                        upstream_leg, ring, upstream_metrics, output);
                },
            },
            {
                "downstream",
                downstream_leg.peer_node_id,
                sidecar::telemetry::LinkDirection::egress,
                telemetry_transport(downstream_leg),
                [&downstream_leg, ring = downstream.get(), &downstream_metrics](
                    sidecar::telemetry::LinkSnapshot& output) noexcept {
                    return snapshot_link(
                        downstream_leg, ring, downstream_metrics, output);
                },
            },
        };

        std::thread telemetry_thread([&] {
            try {
                if (sidecar::telemetry::run_telemetry_exporter(
                        running, targets) != 0) {
                    throw std::runtime_error("telemetry exporter failed");
                }
            } catch (const std::exception& error) {
                std::cerr << "sidecar: telemetry disabled ("
                          << error.what() << ')' << std::endl;
            }
        });

        std::thread upstream_thread;
        std::thread downstream_thread;
        if (upstream_leg.role != LegRole::disabled) {
            upstream_thread = std::thread([&] {
                run_ingress_leg(
                    upstream_leg, upstream.get(), upstream_metrics);
            });
        }
        if (downstream_leg.role != LegRole::disabled) {
            downstream_thread = std::thread([&] {
                run_egress_leg(
                    downstream_leg, downstream.get(), downstream_metrics);
            });
        }

        std::cout << "sidecar: fixed-slot rings ready, upstream="
                  << upstream_config.slot_count << 'x'
                  << upstream_config.max_payload_bytes
                  << " downstream=" << downstream_config.slot_count << 'x'
                  << downstream_config.max_payload_bytes << std::endl;
        std::cout << "sidecar: telemetry started; dual-leg gateway ready"
                  << std::endl;

        while (running != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }

        ringbuf_shutdown(upstream.get());
        ringbuf_shutdown(downstream.get());
        if (upstream_thread.joinable()) {
            upstream_thread.join();
        }
        if (downstream_thread.joinable()) {
            downstream_thread.join();
        }
        telemetry_thread.join();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sidecar: " << error.what() << '\n';
        return 1;
    }
}
