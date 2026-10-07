#include "forwarder/forwarder.hpp"
#include "network/ucx_transport.hpp"
#include "ringbuf/ringbuf.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

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

bool run_peer(
    bool listener,
    bool producer,
    std::uint16_t port,
    RingBuffer* ring) {
    bool transport_connected = false;
    try {
        const sidecar::network::EndpointOptions options{
            "127.0.0.1", port, std::chrono::seconds{10}};
        sidecar::network::UCXTransport transport =
            listener
                ? sidecar::network::UCXTransport::accept_one(options)
                : sidecar::network::UCXTransport::connect(options);
        transport_connected = true;
        auto memory = transport.register_memory(ringbuf_storage(ring));
        volatile std::sig_atomic_t running = 1;
        sidecar::forwarder::LegMetrics metrics;
        if (producer) {
            sidecar::forwarder::run_egress_session(
                running, ring, transport, memory, metrics);
        } else {
            sidecar::forwarder::run_ingress_session(
                running, ring, transport, memory, metrics);
        }
    } catch (const std::exception&) {
        return transport_connected;
    }
    return false;
}

bool contracts_are_rejected(
    const std::string& prefix,
    const RingBufferConfig& producer_config,
    const RingBufferConfig& consumer_config,
    std::uint16_t port) {
    TestRing producer{prefix + "_producer", producer_config};
    TestRing consumer{prefix + "_consumer", consumer_config};
    auto listener = std::async(
        std::launch::async,
        [&] { return run_peer(true, false, port, consumer.get()); });
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    auto connector = std::async(
        std::launch::async,
        [&] { return run_peer(false, true, port, producer.get()); });
    return listener.get() && connector.get();
}

}  // namespace

int main() {
    ::setenv("UCX_TLS", "tcp,self", 1);
    const std::string prefix =
        "/uestcradar_contract_test_" + std::to_string(::getpid());
    constexpr std::uint32_t capacity = 4096;
    TestRing validation_ring{
        prefix + "_frame_validation", {2, capacity, 11, 2}};
    uestcradar::Envelope envelope{
        .frame_id = 1,
        .type_id = 11,
        .type_version = 2,
        .payload_length = 128,
    };
    const bool valid_frame = sidecar::forwarder::frame_contract_is_valid(
        validation_ring.get(), envelope, kSlotHeaderSize + 128);
    envelope.type_version = 3;
    const bool bad_version_rejected =
        !sidecar::forwarder::frame_contract_is_valid(
            validation_ring.get(), envelope, kSlotHeaderSize + 128);
    envelope.type_version = 2;
    envelope.type_id = 12;
    const bool bad_type_rejected =
        !sidecar::forwarder::frame_contract_is_valid(
            validation_ring.get(), envelope, kSlotHeaderSize + 128);
    envelope.type_id = 11;
    const bool bad_length_rejected =
        !sidecar::forwarder::frame_contract_is_valid(
            validation_ring.get(), envelope, kSlotHeaderSize + 127);
    const auto port = static_cast<std::uint16_t>(
        40'000 + (::getpid() % 10'000));
    const bool type_rejected = contracts_are_rejected(
        prefix + "_type",
        {2, capacity, 11, 2},
        {2, capacity, 99, 2},
        port);
    const bool version_rejected = contracts_are_rejected(
        prefix + "_version",
        {2, capacity, 11, 2},
        {2, capacity, 11, 3},
        static_cast<std::uint16_t>(port + 1));
    return valid_frame && bad_version_rejected && bad_type_rejected &&
                   bad_length_rejected && type_rejected && version_rejected
               ? 0
               : 1;
}
