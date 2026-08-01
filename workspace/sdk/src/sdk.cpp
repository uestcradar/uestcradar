#include <sdk.h>

#include "ringbuf/ringbuf.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace uestcradar {
namespace {

enum class LeaseKind {
    none,
    read,
    write,
};

const char* environment_or(
    const char* name,
    const char* fallback) noexcept {
    const char* value = std::getenv(name);
    return value == nullptr || value[0] == '\0' ? fallback : value;
}

struct PortState {
    explicit PortState(RingBuffer* value) noexcept : ring(value) {}

    ~PortState() {
        if (kind == LeaseKind::read) {
            static_cast<void>(ringbuf_release(read_lease));
        } else if (kind == LeaseKind::write) {
            ringbuf_cancel(write_lease);
        }
        ringbuf_close(ring);
    }

    RingBuffer* ring{nullptr};
    RingReadLease read_lease;
    RingWriteLease write_lease;
    LeaseKind kind{LeaseKind::none};
};

std::shared_ptr<PortState> open_port(
    const char* environment_name,
    const char* default_name) {
    return std::make_shared<PortState>(ringbuf_open(
        environment_or(environment_name, default_name)));
}

void wait_for_read(PortState& state) {
    for (;;) {
        const RingResult result =
            ringbuf_acquire(state.ring, state.read_lease);
        if (result == RingResult::ok) {
            return;
        }
        if (result == RingResult::shutdown) {
            throw std::runtime_error("input has been shut down");
        }
        if (result != RingResult::would_block) {
            throw std::runtime_error("input RingBuffer is corrupt");
        }
        std::this_thread::sleep_for(std::chrono::microseconds{50});
    }
}

void wait_for_write(PortState& state) {
    for (;;) {
        const RingResult result =
            ringbuf_reserve(state.ring, state.write_lease);
        if (result == RingResult::ok) {
            return;
        }
        if (result == RingResult::shutdown) {
            throw std::runtime_error("output has been shut down");
        }
        if (result != RingResult::would_block) {
            throw std::runtime_error("output RingBuffer is corrupt");
        }
        std::this_thread::sleep_for(std::chrono::microseconds{50});
    }
}

void validate_envelope(
    const Envelope& envelope,
    const RingBuffer& ring) {
    if (envelope.type_id == 0 || envelope.type_version == 0 ||
        envelope.type_id != ring.header->type_id ||
        envelope.type_version != ring.header->type_version) {
        throw std::invalid_argument(
            "RawFrame contract does not match SDK port");
    }
    if (envelope.payload_length == 0 ||
        envelope.payload_length > ring.header->max_payload_bytes) {
        throw std::invalid_argument(
            "RawFrame payload length exceeds SDK port");
    }
}

}  // namespace

struct RawFrame::Impl {
    explicit Impl(std::shared_ptr<PortState> value) noexcept
        : state(std::move(value)) {}

    ~Impl() {
        if (!state) {
            return;
        }
        if (state->kind == LeaseKind::read) {
            static_cast<void>(ringbuf_release(state->read_lease));
        } else if (state->kind == LeaseKind::write) {
            ringbuf_cancel(state->write_lease);
        }
        state->kind = LeaseKind::none;
    }

    Envelope& envelope() {
        if (state->kind == LeaseKind::read) {
            return const_cast<Envelope&>(state->read_lease.envelope());
        }
        if (state->kind == LeaseKind::write) {
            return state->write_lease.envelope();
        }
        throw std::runtime_error("RawFrame is no longer active");
    }

    const Envelope& envelope() const {
        return const_cast<Impl*>(this)->envelope();
    }

    std::span<std::byte> payload() {
        if (state->kind == LeaseKind::read) {
            const auto bytes = state->read_lease.payload();
            return {
                const_cast<std::byte*>(bytes.data()), bytes.size()};
        }
        if (state->kind == LeaseKind::write) {
            return state->write_lease.payload().first(
                state->write_lease.envelope().payload_length);
        }
        throw std::runtime_error("RawFrame is no longer active");
    }

    std::shared_ptr<PortState> state;
};

struct Input<RawFrame>::Impl {
    Impl()
        : state(open_port(
              "UESTCRADAR_UPSTREAM_SHM_NAME", kUpstreamBufName)) {}

    std::shared_ptr<PortState> state;
};

struct Output<RawFrame>::Impl {
    Impl()
        : state(open_port(
              "UESTCRADAR_DOWNSTREAM_SHM_NAME", kDownstreamBufName)) {}

    std::shared_ptr<PortState> state;
};

RawFrame::RawFrame(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

RawFrame::RawFrame(RawFrame&& other) noexcept = default;
RawFrame& RawFrame::operator=(RawFrame&& other) noexcept = default;
RawFrame::~RawFrame() = default;

Envelope& RawFrame::envelope() {
    if (!impl_) {
        throw std::runtime_error("RawFrame is empty");
    }
    return impl_->envelope();
}

const Envelope& RawFrame::envelope() const {
    if (!impl_) {
        throw std::runtime_error("RawFrame is empty");
    }
    return impl_->envelope();
}

std::span<std::byte> RawFrame::payload_span() {
    if (!impl_) {
        throw std::runtime_error("RawFrame is empty");
    }
    return impl_->payload();
}

std::span<const std::byte> RawFrame::payload_span() const {
    if (!impl_) {
        throw std::runtime_error("RawFrame is empty");
    }
    const auto bytes = impl_->payload();
    return {bytes.data(), bytes.size()};
}

Input<RawFrame>::Input() : impl_(std::make_unique<Impl>()) {}
Input<RawFrame>::Input(Input&& other) noexcept = default;
Input<RawFrame>& Input<RawFrame>::operator=(Input&& other) noexcept = default;
Input<RawFrame>::~Input() = default;

RawFrame Input<RawFrame>::read() {
    if (!impl_) {
        throw std::runtime_error("input port is not open");
    }
    PortState& state = *impl_->state;
    if (state.kind != LeaseKind::none) {
        throw std::runtime_error("the previous input frame is still alive");
    }
    wait_for_read(state);
    try {
        validate_envelope(state.read_lease.envelope(), *state.ring);
        state.kind = LeaseKind::read;
        return RawFrame{std::make_unique<RawFrame::Impl>(impl_->state)};
    } catch (...) {
        static_cast<void>(ringbuf_release(state.read_lease));
        throw;
    }
}

Output<RawFrame>::Output() : impl_(std::make_unique<Impl>()) {}
Output<RawFrame>::Output(Output&& other) noexcept = default;
Output<RawFrame>& Output<RawFrame>::operator=(Output&& other) noexcept =
    default;
Output<RawFrame>::~Output() = default;

RawFrame Output<RawFrame>::create(const Envelope& envelope) {
    if (!impl_) {
        throw std::runtime_error("output port is not open");
    }
    PortState& state = *impl_->state;
    if (state.kind != LeaseKind::none) {
        throw std::runtime_error("the previous output frame is still alive");
    }
    validate_envelope(envelope, *state.ring);
    wait_for_write(state);
    state.write_lease.envelope() = envelope;
    for (std::byte& value : state.write_lease.envelope().reserved) {
        value = std::byte{};
    }
    state.kind = LeaseKind::write;
    return RawFrame{std::make_unique<RawFrame::Impl>(impl_->state)};
}

void Output<RawFrame>::write(RawFrame&& frame) {
    if (!impl_ || !frame.impl_ ||
        frame.impl_->state.get() != impl_->state.get()) {
        throw std::invalid_argument(
            "output RawFrame does not belong to this port");
    }
    PortState& state = *impl_->state;
    if (state.kind != LeaseKind::write) {
        throw std::runtime_error("output RawFrame is not writable");
    }
    validate_envelope(state.write_lease.envelope(), *state.ring);
    if (ringbuf_commit(state.write_lease) != RingResult::ok) {
        throw std::runtime_error("could not commit output RawFrame");
    }
    state.kind = LeaseKind::none;
    frame.impl_->state.reset();
    frame.impl_.reset();
}

}  // namespace uestcradar
