#include "forwarder.hpp"

#include "forwarder_protocol.hpp"
#include "network/ucx_transport.hpp"
#include "ringbuf/ringbuf.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>

namespace sidecar::forwarder {
namespace {

using network::UCXMemoryRegion;
using network::UCXRequest;
using network::UCXTransport;

protocol::PortContract contract(const RingBuffer* ring) {
    return {
        ring->header->type_id,
        ring->header->type_version,
        static_cast<std::uint32_t>(
            ring->header->max_payload_bytes),
    };
}

void exchange_and_validate_contract(
    RingBuffer* ring,
    protocol::PortRole local_role,
    UCXTransport& transport) {
    const protocol::Hello local{local_role, contract(ring)};
    protocol::HelloBytes outgoing = protocol::encode_hello(local);
    protocol::HelloBytes incoming{};
    UCXRequest receive =
        transport.receive(incoming, protocol::kHelloTag);
    UCXRequest send =
        transport.send(outgoing, protocol::kHelloTag);
    transport.wait(send);
    transport.wait(receive);
    if (receive.bytes_transferred() != incoming.size()) {
        throw std::runtime_error("forwarder hello has invalid length");
    }

    protocol::Hello remote{};
    if (!protocol::decode_hello(incoming, remote)) {
        throw std::runtime_error("forwarder hello is invalid");
    }
    if (remote.role == local.role) {
        throw std::runtime_error(
            "forwarder peer has the same port role");
    }

    const protocol::PortContract& producer =
        local.role == protocol::PortRole::producer
            ? local.contract
            : remote.contract;
    const protocol::PortContract& consumer =
        local.role == protocol::PortRole::consumer
            ? local.contract
            : remote.contract;
    if (producer.type_id != consumer.type_id ||
        producer.type_version != consumer.type_version ||
        producer.max_payload_bytes > consumer.max_payload_bytes) {
        throw std::runtime_error(
            "forwarder peer port contracts are incompatible");
    }
}

class EgressPump {
public:
    EgressPump(
        RingBuffer* ring,
        UCXTransport& transport,
        const UCXMemoryRegion& memory,
        LegMetrics& metrics)
        : ring_(ring),
          transport_(transport),
          memory_(memory),
          metrics_(metrics) {}

    ~EgressPump() {
        if (read_lease_.active()) {
            static_cast<void>(ringbuf_release(read_lease_));
        }
    }

    bool progress() {
        bool activity = false;
        if (!read_lease_.active()) {
            const RingResult result =
                ringbuf_acquire(ring_, read_lease_);
            if (result == RingResult::ok) {
                activity = true;
            } else if (result != RingResult::would_block &&
                       result != RingResult::shutdown) {
                throw std::runtime_error(
                    "egress could not acquire source slot");
            }
        }
        if (!credit_receive_active_ && !credit_available_) {
            credit_receive_ = transport_.receive(
                credit_bytes_, protocol::kCreditTag);
            credit_receive_active_ = true;
            activity = true;
        }
        if (credit_receive_active_ && credit_receive_.completed()) {
            transport_.wait(credit_receive_);
            if (credit_receive_.bytes_transferred() !=
                credit_bytes_.size()) {
                throw std::runtime_error("invalid credit message");
            }
            const std::uint64_t decoded =
                protocol::decode_credit(credit_bytes_);
            if (decoded == 0 ||
                decoded > std::numeric_limits<std::size_t>::max()) {
                throw std::runtime_error("invalid credit value");
            }
            credit_ = static_cast<std::size_t>(decoded);
            credit_receive_active_ = false;
            credit_available_ = true;
            activity = true;
        }
        if (read_lease_.active() && credit_available_ &&
            !payload_send_active_) {
            const auto frame = read_lease_.frame();
            const auto& envelope = read_lease_.envelope();
            if (!frame_contract_is_valid(
                    ring_, envelope, frame.size())) {
                throw std::runtime_error(
                    "egress RawFrame contract is invalid");
            }
            if (frame.size() > credit_) {
                throw std::runtime_error(
                    "RawFrame exceeds peer receive slot");
            }
            payload_length_ = envelope.payload_length;
            frame_length_ = frame.size();
            payload_send_ = transport_.send(
                frame,
                protocol::kPayloadTag,
                &memory_);
            payload_send_active_ = true;
            activity = true;
        }
        if (payload_send_active_ && payload_send_.completed()) {
            transport_.wait(payload_send_);
            if (payload_send_.bytes_transferred() != frame_length_) {
                throw std::runtime_error(
                    "egress RawFrame length mismatch");
            }
            if (ringbuf_release(read_lease_) != RingResult::ok) {
                throw std::runtime_error(
                    "egress could not release sent slot");
            }
            metrics_.payload_bytes_total.fetch_add(
                payload_length_, std::memory_order_relaxed);
            payload_send_active_ = false;
            credit_available_ = false;
            credit_ = 0;
            payload_length_ = 0;
            frame_length_ = 0;
            activity = true;
        }
        return activity;
    }

private:
    RingBuffer* ring_;
    UCXTransport& transport_;
    const UCXMemoryRegion& memory_;
    LegMetrics& metrics_;
    RingReadLease read_lease_;
    protocol::CreditBytes credit_bytes_{};
    UCXRequest credit_receive_;
    UCXRequest payload_send_;
    std::size_t credit_{0};
    std::size_t payload_length_{0};
    std::size_t frame_length_{0};
    bool credit_receive_active_{false};
    bool credit_available_{false};
    bool payload_send_active_{false};
};

class IngressPump {
public:
    IngressPump(
        RingBuffer* ring,
        UCXTransport& transport,
        const UCXMemoryRegion& memory,
        LegMetrics& metrics)
        : ring_(ring),
          transport_(transport),
          memory_(memory),
          metrics_(metrics) {}

    ~IngressPump() {
        ringbuf_cancel(write_lease_);
    }

    bool progress() {
        bool activity = false;
        if (!active_) {
            const RingResult result =
                ringbuf_reserve(ring_, write_lease_);
            if (result == RingResult::ok) {
                const auto capacity = write_lease_.frame_capacity();
                payload_receive_ = transport_.receive(
                    capacity,
                    protocol::kPayloadTag,
                    UINT64_MAX,
                    &memory_);
                credit_bytes_ = protocol::encode_credit(capacity.size());
                credit_send_ = transport_.send(
                    credit_bytes_, protocol::kCreditTag);
                active_ = true;
                activity = true;
            } else if (result != RingResult::would_block &&
                       result != RingResult::shutdown) {
                throw std::runtime_error(
                    "ingress could not reserve destination slot");
            }
        }
        if (active_ && !payload_committed_ &&
            payload_receive_.completed()) {
            try {
                transport_.wait(payload_receive_);
                const std::size_t received =
                    payload_receive_.bytes_transferred();
                const auto& envelope = write_lease_.envelope();
                const std::size_t payload_length =
                    envelope.payload_length;
                const bool valid = frame_contract_is_valid(
                    ring_, envelope, received);
                if (!valid ||
                    ringbuf_commit(write_lease_) !=
                        RingResult::ok) {
                    throw std::runtime_error(
                        "ingress received invalid RawFrame");
                }
                metrics_.payload_bytes_total.fetch_add(
                    payload_length, std::memory_order_relaxed);
            } catch (...) {
                ringbuf_cancel(write_lease_);
                throw;
            }
            payload_committed_ = true;
            activity = true;
        }
        if (active_ && !credit_completed_ &&
            credit_send_.completed()) {
            transport_.wait(credit_send_);
            credit_completed_ = true;
            activity = true;
        }
        if (active_ && payload_committed_ && credit_completed_) {
            active_ = false;
            payload_committed_ = false;
            credit_completed_ = false;
            activity = true;
        }
        return activity;
    }

private:
    RingBuffer* ring_;
    UCXTransport& transport_;
    const UCXMemoryRegion& memory_;
    LegMetrics& metrics_;
    RingWriteLease write_lease_;
    protocol::CreditBytes credit_bytes_{};
    UCXRequest credit_send_;
    UCXRequest payload_receive_;
    bool active_{false};
    bool payload_committed_{false};
    bool credit_completed_{false};
};

class IdleBackoff {
public:
    void update(bool activity) {
        if (activity) {
            idle_iterations_ = 0;
        } else if (++idle_iterations_ <= 64) {
            return;
        } else if (idle_iterations_ <= 128) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds{50});
        }
    }

private:
    std::size_t idle_iterations_{0};
};

template <class Pump>
void run_session(
    volatile std::sig_atomic_t& running,
    RingBuffer* ring,
    UCXTransport& transport,
    Pump& pump) {
    IdleBackoff backoff;
    while (running != 0 && !ringbuf_is_shutdown(ring)) {
        bool activity = transport.progress();
        activity = pump.progress() || activity;
        backoff.update(activity);
    }
}

}  // namespace

namespace {

class ConnectedSession {
public:
    explicit ConnectedSession(LegMetrics& metrics) noexcept
        : metrics_(metrics) {
        metrics_.connected.store(true, std::memory_order_release);
    }

    ConnectedSession(const ConnectedSession&) = delete;
    ConnectedSession& operator=(const ConnectedSession&) = delete;

    ~ConnectedSession() {
        metrics_.connected.store(false, std::memory_order_release);
    }

private:
    LegMetrics& metrics_;
};

}  // namespace

bool frame_contract_is_valid(
    const RingBuffer* ring,
    const uestcradar::Envelope& envelope,
    std::size_t frame_length) noexcept {
    return ring != nullptr && ring->header != nullptr &&
           envelope.type_id == ring->header->type_id &&
           envelope.type_version == ring->header->type_version &&
           envelope.payload_length != 0 &&
           envelope.payload_length <= ring->header->max_payload_bytes &&
           frame_length == kSlotHeaderSize + envelope.payload_length;
}

void run_ingress_session(
    volatile std::sig_atomic_t& running,
    RingBuffer* input,
    UCXTransport& transport,
    const UCXMemoryRegion& input_memory,
    LegMetrics& metrics) {
    if (input == nullptr || !input_memory.valid()) {
        throw std::invalid_argument(
            "ingress requires a ring and registered memory");
    }
    exchange_and_validate_contract(
        input, protocol::PortRole::consumer, transport);
    ConnectedSession connected{metrics};
    IngressPump pump{input, transport, input_memory, metrics};
    run_session(running, input, transport, pump);
}

void run_egress_session(
    volatile std::sig_atomic_t& running,
    RingBuffer* output,
    UCXTransport& transport,
    const UCXMemoryRegion& output_memory,
    LegMetrics& metrics) {
    if (output == nullptr || !output_memory.valid()) {
        throw std::invalid_argument(
            "egress requires a ring and registered memory");
    }
    exchange_and_validate_contract(
        output, protocol::PortRole::producer, transport);
    ConnectedSession connected{metrics};
    EgressPump pump{output, transport, output_memory, metrics};
    run_session(running, output, transport, pump);
}

DroppedFrames drop_stale_frames(RingBuffer* output) noexcept {
    DroppedFrames dropped;
    if (output == nullptr) {
        return dropped;
    }
    for (;;) {
        RingReadLease lease;
        const RingResult result = ringbuf_acquire(output, lease);
        if (result != RingResult::ok) {
            return dropped;
        }
        ++dropped.frames;
        dropped.bytes += lease.payload().size();
        if (ringbuf_release(lease) != RingResult::ok) {
            return dropped;
        }
    }
}

}  // namespace sidecar::forwarder
