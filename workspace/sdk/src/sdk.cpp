#include <data.h>

#include "contract_runtime.hpp"
#include "contract_traits.generated.hpp"
#include "ringbuf/ringbuf.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace uestcradar {
namespace sdk_internal {

using internal::ContractTraits;

static_assert(sizeof(ComplexInt16) == 4);
static_assert(sizeof(ComplexFloat32) == 8);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<double>::is_iec559);

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
    const char* default_name,
    std::uint64_t type_id,
    std::uint32_t type_version) {
    auto state = std::make_shared<PortState>(ringbuf_open(
        environment_or(environment_name, default_name)));
    if (state->ring->header->type_id != type_id ||
        state->ring->header->type_version != type_version) {
        throw std::invalid_argument(
            "shared-memory port has a different data contract");
    }
    return state;
}

void wait_for_read(PortState& state) {
    for (;;) {
        const RingResult result = ringbuf_acquire(state.ring, state.read_lease);
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
        const RingResult result = ringbuf_reserve(state.ring, state.write_lease);
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

std::uint32_t narrow_payload_length(std::size_t length) {
    if (length > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("frame payload is too large");
    }
    return static_cast<std::uint32_t>(length);
}

std::uint64_t unix_time_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

Envelope make_envelope(
    std::uint64_t type_id,
    std::uint32_t type_version,
    std::size_t length,
    std::uint64_t frame_id,
    std::uint64_t timestamp) {
    return {
        .frame_id = frame_id,
        .timestamp = timestamp,
        .type_id = type_id,
        .type_version = type_version,
        .payload_length = narrow_payload_length(length),
    };
}

void validate_envelope(
    const Envelope& envelope,
    const RingBuffer& ring,
    std::uint64_t type_id,
    std::uint32_t type_version,
    std::size_t expected_payload) {
    if (envelope.type_id != type_id ||
        envelope.type_version != type_version ||
        envelope.type_id != ring.header->type_id ||
        envelope.type_version != ring.header->type_version) {
        throw std::invalid_argument("frame does not match the port contract");
    }
    if (envelope.payload_length != expected_payload ||
        envelope.payload_length > ring.header->max_payload_bytes) {
        throw std::invalid_argument("frame payload length is invalid");
    }
}

void begin_output(
    PortState& state,
    const Envelope& envelope,
    std::uint64_t type_id,
    std::uint32_t type_version,
    std::size_t expected_payload) {
    if (state.kind != LeaseKind::none) {
        throw std::runtime_error("the previous output frame is still alive");
    }
    validate_envelope(
        envelope, *state.ring, type_id, type_version, expected_payload);
    wait_for_write(state);
    state.write_lease.envelope() = envelope;
    std::fill(
        std::begin(state.write_lease.envelope().reserved),
        std::end(state.write_lease.envelope().reserved),
        std::byte{});
    state.kind = LeaseKind::write;
}

}  // namespace sdk_internal

using namespace sdk_internal;

struct FrameStorage {
    explicit FrameStorage(std::shared_ptr<PortState> value) noexcept
        : state(std::move(value)) {}

    ~FrameStorage() {
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

    const Envelope& envelope() const {
        if (state && state->kind == LeaseKind::read) {
            return state->read_lease.envelope();
        }
        if (state && state->kind == LeaseKind::write) {
            return state->write_lease.envelope();
        }
        throw std::runtime_error("frame is no longer active");
    }

    std::span<std::byte> payload() {
        if (state && state->kind == LeaseKind::read) {
            const auto bytes = state->read_lease.payload();
            return {const_cast<std::byte*>(bytes.data()), bytes.size()};
        }
        if (state && state->kind == LeaseKind::write) {
            return state->write_lease.payload().first(
                state->write_lease.envelope().payload_length);
        }
        throw std::runtime_error("frame is no longer active");
    }

    std::span<const std::byte> payload() const {
        const auto bytes = const_cast<FrameStorage*>(this)->payload();
        return {bytes.data(), bytes.size()};
    }

    std::shared_ptr<PortState> state;
};

#define UESTCRADAR_CONTRACT(Name, FrameType, MetadataType)                         \
    struct FrameType::Impl final : FrameStorage {                                 \
        using FrameStorage::FrameStorage;                                         \
    };                                                                            \
    FrameType::FrameType(std::unique_ptr<Impl> value) noexcept                    \
        : impl_(std::move(value)) {}                                              \
    FrameType::FrameType(FrameType&& other) noexcept = default;                   \
    FrameType& FrameType::operator=(FrameType&& other) noexcept = default;        \
    FrameType::~FrameType() = default;                                            \
    const void* FrameType::sdk_trace_context() const {                            \
        if (!impl_) throw std::runtime_error("frame is empty");                 \
        return impl_.get();                                                       \
    }                                                                             \
    MetadataType FrameType::metadata() const {                                    \
        if (!impl_) throw std::runtime_error("frame is empty");                 \
        const MetadataType value = ContractTraits<FrameType>::load(impl_->payload()); \
        if (ContractTraits<FrameType>::payload_bytes(value) != impl_->payload().size()) \
            throw std::invalid_argument("frame metadata does not match its payload"); \
        return value;                                                             \
    }                                                                             \
    Array2D<typename ContractTraits<FrameType>::Element> FrameType::data() {       \
        const MetadataType value = metadata();                                    \
        return {reinterpret_cast<typename ContractTraits<FrameType>::Element*>(   \
                    impl_->payload().data() + ContractTraits<FrameType>::metadata_bytes()), \
                ContractTraits<FrameType>::rows(value),                           \
                ContractTraits<FrameType>::columns(value)};                       \
    }                                                                             \
    Array2D<const typename ContractTraits<FrameType>::Element> FrameType::data() const { \
        const MetadataType value = metadata();                                    \
        return {reinterpret_cast<const typename ContractTraits<FrameType>::Element*>( \
                    impl_->payload().data() + ContractTraits<FrameType>::metadata_bytes()), \
                ContractTraits<FrameType>::rows(value),                           \
                ContractTraits<FrameType>::columns(value)};                       \
    }
#include <contract_catalog.def>
#undef UESTCRADAR_CONTRACT

#define UESTCRADAR_CONTRACT(Name, FrameType, MetadataType)                         \
    struct Input<FrameType>::Impl {                                                \
        Impl() : state(open_port(                                                 \
            "UESTCRADAR_UPSTREAM_SHM_NAME", kUpstreamBufName,                   \
            ContractTraits<FrameType>::type_id(),                                 \
            ContractTraits<FrameType>::type_version())) {}                        \
        std::shared_ptr<PortState> state;                                          \
    };                                                                            \
    Input<FrameType>::Input() : impl_(std::make_unique<Impl>()) {}                \
    Input<FrameType>::Input(Input&& other) noexcept = default;                    \
    Input<FrameType>& Input<FrameType>::operator=(Input&& other) noexcept = default; \
    Input<FrameType>::~Input() = default;                                         \
    FrameType Input<FrameType>::read() {                                          \
        if (!impl_) throw std::runtime_error("input port is not open");         \
        PortState& state = *impl_->state;                                         \
        if (state.kind != LeaseKind::none)                                        \
            throw std::runtime_error("the previous input frame is still alive"); \
        wait_for_read(state);                                                     \
        try {                                                                     \
            const auto bytes = state.read_lease.payload();                        \
            const Envelope& envelope = state.read_lease.envelope();               \
            if (bytes.size() < ContractTraits<FrameType>::metadata_bytes() ||     \
                envelope.type_id != ContractTraits<FrameType>::type_id() ||       \
                envelope.type_version != ContractTraits<FrameType>::type_version()) \
                throw std::invalid_argument("input frame contract is invalid"); \
            state.kind = LeaseKind::read;                                         \
            FrameType frame{std::make_unique<FrameType::Impl>(impl_->state)};     \
            const auto metadata = frame.metadata();                               \
            validate_envelope(envelope, *state.ring,                              \
                ContractTraits<FrameType>::type_id(),                             \
                ContractTraits<FrameType>::type_version(),                        \
                ContractTraits<FrameType>::payload_bytes(metadata));              \
            return frame;                                                         \
        } catch (...) {                                                           \
            if (state.kind == LeaseKind::read) state.kind = LeaseKind::none;      \
            if (state.read_lease.active())                                        \
                static_cast<void>(ringbuf_release(state.read_lease));             \
            throw;                                                               \
        }                                                                         \
    }
#include <contract_catalog.def>
#undef UESTCRADAR_CONTRACT

#define UESTCRADAR_CONTRACT(Name, FrameType, MetadataType)                         \
    struct Output<FrameType>::Impl {                                               \
        Impl() : state(open_port(                                                  \
            "UESTCRADAR_DOWNSTREAM_SHM_NAME", kDownstreamBufName,                \
            ContractTraits<FrameType>::type_id(),                                 \
            ContractTraits<FrameType>::type_version())) {}                        \
        std::shared_ptr<PortState> state;                                          \
        std::uint64_t sequence{0};                                                 \
    };                                                                            \
    Output<FrameType>::Output() : impl_(std::make_unique<Impl>()) {}              \
    Output<FrameType>::Output(Output&& other) noexcept = default;                 \
    Output<FrameType>& Output<FrameType>::operator=(Output&& other) noexcept = default; \
    Output<FrameType>::~Output() = default;                                        \
    FrameType Output<FrameType>::create(const MetadataType& metadata) {            \
        if (!impl_) throw std::runtime_error("output port is not open");         \
        const std::size_t length = ContractTraits<FrameType>::payload_bytes(metadata); \
        const Envelope envelope = make_envelope(                                  \
            ContractTraits<FrameType>::type_id(),                                 \
            ContractTraits<FrameType>::type_version(), length,                    \
            ++impl_->sequence, unix_time_ns());                                   \
        begin_output(*impl_->state, envelope,                                     \
            ContractTraits<FrameType>::type_id(),                                 \
            ContractTraits<FrameType>::type_version(), length);                   \
        try {                                                                     \
            ContractTraits<FrameType>::store(impl_->state->write_lease.payload(), metadata); \
            return FrameType{std::make_unique<FrameType::Impl>(impl_->state)};    \
        } catch (...) {                                                           \
            ringbuf_cancel(impl_->state->write_lease);                            \
            impl_->state->kind = LeaseKind::none;                                 \
            throw;                                                               \
        }                                                                         \
    }                                                                             \
    FrameType Output<FrameType>::create_linked(                                   \
        const MetadataType& metadata, const void* trace_context) {                \
        if (!impl_) throw std::runtime_error("output port is not open");         \
        if (!trace_context) throw std::runtime_error("parent frame is empty");   \
        const auto& parent = *static_cast<const FrameStorage*>(trace_context);     \
        const Envelope& parent_envelope = parent.envelope();                      \
        const std::size_t length = ContractTraits<FrameType>::payload_bytes(metadata); \
        const Envelope envelope = make_envelope(                                  \
            ContractTraits<FrameType>::type_id(),                                 \
            ContractTraits<FrameType>::type_version(), length,                    \
            parent_envelope.frame_id, parent_envelope.timestamp);                 \
        begin_output(*impl_->state, envelope,                                     \
            ContractTraits<FrameType>::type_id(),                                 \
            ContractTraits<FrameType>::type_version(), length);                   \
        try {                                                                     \
            ContractTraits<FrameType>::store(impl_->state->write_lease.payload(), metadata); \
            return FrameType{std::make_unique<FrameType::Impl>(impl_->state)};    \
        } catch (...) {                                                           \
            ringbuf_cancel(impl_->state->write_lease);                            \
            impl_->state->kind = LeaseKind::none;                                 \
            throw;                                                               \
        }                                                                         \
    }                                                                             \
    void Output<FrameType>::write(FrameType&& frame) {                            \
        if (!impl_ || !frame.impl_ || frame.impl_->state.get() != impl_->state.get()) \
            throw std::invalid_argument("output frame does not belong to this port"); \
        PortState& state = *impl_->state;                                          \
        if (state.kind != LeaseKind::write)                                       \
            throw std::runtime_error("output frame is not writable");           \
        const auto metadata = frame.metadata();                                   \
        validate_envelope(state.write_lease.envelope(), *state.ring,              \
            ContractTraits<FrameType>::type_id(),                                 \
            ContractTraits<FrameType>::type_version(),                            \
            ContractTraits<FrameType>::payload_bytes(metadata));                  \
        if (ringbuf_commit(state.write_lease) != RingResult::ok)                  \
            throw std::runtime_error("could not commit output frame");          \
        state.kind = LeaseKind::none;                                             \
        frame.impl_->state.reset();                                               \
        frame.impl_.reset();                                                      \
    }
#include <contract_catalog.def>
#undef UESTCRADAR_CONTRACT

}  // namespace uestcradar
