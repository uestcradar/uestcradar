#include "forwarder/forwarder.hpp"
#include "network/ucx_transport.hpp"
#include "ringbuf/ringbuf.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using sidecar::network::EndpointOptions;
using sidecar::network::UCXMemoryRegion;
using sidecar::network::UCXTransport;

class TestRing {
public:
    TestRing(std::string name, const RingBufferConfig& config)
        : name_(std::move(name)),
          ring_(ringbuf_create(name_.c_str(), config)) {}

    ~TestRing() {
        ringbuf_shutdown(ring_);
        ringbuf_close(ring_);
        ringbuf_unlink(name_.c_str());
    }

    RingBuffer* get() const noexcept {
        return ring_;
    }

private:
    std::string name_;
    RingBuffer* ring_;
};

bool write_record(RingBuffer* ring, const std::vector<std::byte>& data) {
    for (int attempt = 0; attempt < 20'000; ++attempt) {
        RingWriteLease lease;
        const RingResult result = ringbuf_reserve(ring, lease);
        if (result == RingResult::ok) {
            std::copy(data.begin(), data.end(), lease.payload().begin());
            return ringbuf_commit(lease, data.size()) == RingResult::ok;
        }
        if (result != RingResult::would_block) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds{50});
    }
    return false;
}

bool read_record(RingBuffer* ring, std::vector<std::byte>& data) {
    for (int attempt = 0; attempt < 20'000; ++attempt) {
        RingReadLease lease;
        const RingResult result = ringbuf_acquire(ring, lease);
        if (result == RingResult::ok) {
            data.assign(lease.payload().begin(), lease.payload().end());
            return ringbuf_release(lease) == RingResult::ok;
        }
        if (result != RingResult::would_block) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds{50});
    }
    return false;
}

}  // namespace

int main() {
    ::setenv("UCX_TLS", "tcp,self", 1);
    const std::string prefix =
        "/uestcradar_forwarder_test_" + std::to_string(::getpid());
    constexpr std::uint32_t kPayloadBytes = 256 * 1024;
    const RingBufferConfig config{4, kPayloadBytes, 17, 3};
    TestRing source{prefix + "_source", config};
    TestRing destination{prefix + "_destination", config};
    const auto port = static_cast<std::uint16_t>(
        20'000 + (::getpid() % 20'000));
    volatile std::sig_atomic_t egress_running = 1;
    volatile std::sig_atomic_t ingress_running = 1;
    sidecar::forwarder::LegMetrics egress_metrics;
    sidecar::forwarder::LegMetrics ingress_metrics;
    std::exception_ptr egress_error;
    std::exception_ptr ingress_error;

    std::thread ingress([&] {
        try {
            UCXTransport transport = UCXTransport::accept_one(
                EndpointOptions{
                    "127.0.0.1", port, std::chrono::seconds{10}});
            UCXMemoryRegion memory = transport.register_memory(
                ringbuf_storage(destination.get()));
            sidecar::forwarder::run_ingress_session(
                ingress_running,
                destination.get(),
                transport,
                memory,
                ingress_metrics);
        } catch (...) {
            ingress_error = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    std::thread egress([&] {
        try {
            UCXTransport transport = UCXTransport::connect(
                EndpointOptions{
                    "127.0.0.1", port, std::chrono::seconds{10}});
            UCXMemoryRegion memory = transport.register_memory(
                ringbuf_storage(source.get()));
            sidecar::forwarder::run_egress_session(
                egress_running,
                source.get(),
                transport,
                memory,
                egress_metrics);
        } catch (...) {
            egress_error = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    std::vector<std::byte> expected(kPayloadBytes);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expected[index] = static_cast<std::byte>((index * 17 + 3) & 0xff);
    }
    std::vector<std::byte> actual;
    const bool transfer_ok =
        write_record(source.get(), expected) &&
        read_record(destination.get(), actual);
    const bool sessions_connected =
        egress_metrics.connected.load(std::memory_order_acquire) &&
        ingress_metrics.connected.load(std::memory_order_acquire);

    egress_running = 0;
    ingress_running = 0;
    ringbuf_shutdown(source.get());
    ringbuf_shutdown(destination.get());
    egress.join();
    ingress.join();

    if (egress_error != nullptr || ingress_error != nullptr) {
        try {
            if (egress_error != nullptr) {
                std::rethrow_exception(egress_error);
            }
            std::rethrow_exception(ingress_error);
        } catch (const std::exception& error) {
            std::cerr << "forwarder-test: " << error.what() << '\n';
        }
        return 1;
    }
    if (!transfer_ok || !sessions_connected || actual != expected) {
        std::cerr << "forwarder-test: payload mismatch\n";
        return 1;
    }
    if (egress_metrics.payload_bytes_total.load() != expected.size() ||
        ingress_metrics.payload_bytes_total.load() != expected.size() ||
        egress_metrics.connected.load() || ingress_metrics.connected.load()) {
        std::cerr << "forwarder-test: telemetry metrics mismatch\n";
        return 1;
    }
    return 0;
}
