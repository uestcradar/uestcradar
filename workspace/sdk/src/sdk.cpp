#include <data.h>

#include "ringbuf/ringbuf.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace uestcradar {
namespace {

constexpr std::uint32_t kContractVersion = 2;
constexpr std::uint64_t kIQTypeId = 1;
constexpr std::uint64_t kPulseCompressionTypeId = 2;
constexpr std::uint64_t kRDTypeId = 3;

enum class LeaseKind {
    none,
    read,
    write,
};

enum class FrameKind {
    iq,
    pulse_compression,
    rd,
};

struct RDWireMetadata {
    std::uint32_t channel_index{};
    std::uint32_t range_bin_count{};
    std::uint32_t doppler_bin_count{};
    std::uint32_t reserved{};
    double range_resolution_m{};
    double velocity_resolution_mps{};
};

static_assert(sizeof(ComplexInt16) == 4);
static_assert(sizeof(ComplexFloat32) == 8);
static_assert(sizeof(IQMetadata) == 24);
static_assert(sizeof(PulseCompressionMetadata) == 24);
static_assert(sizeof(RDMetadata) == 32);
static_assert(sizeof(RDWireMetadata) == 32);
static_assert(std::is_trivially_copyable_v<IQMetadata>);
static_assert(std::is_trivially_copyable_v<PulseCompressionMetadata>);
static_assert(std::is_trivially_copyable_v<RDWireMetadata>);

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
    std::uint64_t type_id) {
    auto state = std::make_shared<PortState>(ringbuf_open(
        environment_or(environment_name, default_name)));
    if (state->ring->header->type_id != type_id ||
        state->ring->header->type_version != kContractVersion) {
        throw std::invalid_argument(
            "shared-memory port has a different data contract");
    }
    return state;
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

std::size_t checked_payload_bytes(
    std::size_t metadata_bytes,
    std::size_t rows,
    std::size_t columns,
    std::size_t element_bytes,
    const char* name) {
    if (rows == 0 || columns == 0 ||
        rows > std::numeric_limits<std::size_t>::max() / columns) {
        throw std::invalid_argument(
            std::string{name} + " data dimensions are invalid");
    }
    const std::size_t elements = rows * columns;
    if (elements >
        (std::numeric_limits<std::size_t>::max() - metadata_bytes) /
            element_bytes) {
        throw std::invalid_argument(
            std::string{name} + " data size overflows");
    }
    return metadata_bytes + elements * element_bytes;
}

std::size_t payload_bytes(const IQMetadata& metadata) {
    return checked_payload_bytes(
        sizeof(IQMetadata),
        metadata.channel_count,
        metadata.samples_per_channel,
        sizeof(ComplexInt16),
        "IQ");
}

std::size_t payload_bytes(const PulseCompressionMetadata& metadata) {
    return checked_payload_bytes(
        sizeof(PulseCompressionMetadata),
        metadata.channel_count,
        metadata.range_bin_count,
        sizeof(ComplexFloat32),
        "pulse compression");
}

std::size_t payload_bytes(const RDMetadata& metadata) {
    return checked_payload_bytes(
        sizeof(RDWireMetadata),
        metadata.range_bin_count,
        metadata.doppler_bin_count,
        sizeof(float),
        "RD");
}

std::uint32_t narrow_payload_length(std::size_t length) {
    if (length > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("frame payload is too large");
    }
    return static_cast<std::uint32_t>(length);
}

std::uint64_t type_id(FrameKind kind) noexcept {
    switch (kind) {
        case FrameKind::iq:
            return kIQTypeId;
        case FrameKind::pulse_compression:
            return kPulseCompressionTypeId;
        case FrameKind::rd:
            return kRDTypeId;
    }
    return 0;
}

std::uint64_t unix_time_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void validate_envelope(
    const Envelope& envelope,
    const RingBuffer& ring,
    FrameKind kind,
    std::size_t expected_payload) {
    if (envelope.type_id != type_id(kind) ||
        envelope.type_version != kContractVersion ||
        envelope.type_id != ring.header->type_id ||
        envelope.type_version != ring.header->type_version) {
        throw std::invalid_argument("frame does not match the port contract");
    }
    if (envelope.payload_length != expected_payload ||
        envelope.payload_length > ring.header->max_payload_bytes) {
        throw std::invalid_argument("frame payload length is invalid");
    }
}

template <class Metadata>
void store_metadata(std::span<std::byte> payload, const Metadata& metadata) {
    std::memcpy(payload.data(), &metadata, sizeof(metadata));
}

void store_metadata(
    std::span<std::byte> payload,
    const RDMetadata& metadata) {
    const RDWireMetadata wire{
        .channel_index = metadata.channel_index,
        .range_bin_count = metadata.range_bin_count,
        .doppler_bin_count = metadata.doppler_bin_count,
        .reserved = 0,
        .range_resolution_m = metadata.range_resolution_m,
        .velocity_resolution_mps = metadata.velocity_resolution_mps,
    };
    std::memcpy(payload.data(), &wire, sizeof(wire));
}

template <class Metadata>
Metadata load_metadata(std::span<const std::byte> payload) {
    Metadata metadata{};
    std::memcpy(&metadata, payload.data(), sizeof(metadata));
    return metadata;
}

template <>
RDMetadata load_metadata<RDMetadata>(std::span<const std::byte> payload) {
    RDWireMetadata wire{};
    std::memcpy(&wire, payload.data(), sizeof(wire));
    if (wire.reserved != 0) {
        throw std::invalid_argument("RD reserved metadata is not zero");
    }
    return {
        .channel_index = wire.channel_index,
        .range_bin_count = wire.range_bin_count,
        .doppler_bin_count = wire.doppler_bin_count,
        .range_resolution_m = wire.range_resolution_m,
        .velocity_resolution_mps = wire.velocity_resolution_mps,
    };
}

Envelope make_envelope(
    FrameKind kind,
    std::size_t length,
    std::uint64_t frame_id,
    std::uint64_t timestamp) {
    return {
        .frame_id = frame_id,
        .timestamp = timestamp,
        .type_id = type_id(kind),
        .type_version = kContractVersion,
        .payload_length = narrow_payload_length(length),
    };
}

void begin_output(
    PortState& state,
    const Envelope& envelope,
    FrameKind kind,
    std::size_t expected_payload) {
    if (state.kind != LeaseKind::none) {
        throw std::runtime_error("the previous output frame is still alive");
    }
    validate_envelope(envelope, *state.ring, kind, expected_payload);
    wait_for_write(state);
    state.write_lease.envelope() = envelope;
    std::fill(
        std::begin(state.write_lease.envelope().reserved),
        std::end(state.write_lease.envelope().reserved),
        std::byte{});
    state.kind = LeaseKind::write;
}

}  // namespace

struct Frame::Impl {
    Impl(std::shared_ptr<PortState> value, FrameKind value_kind) noexcept
        : state(std::move(value)), kind(value_kind) {}

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
        const auto bytes = const_cast<Impl*>(this)->payload();
        return {bytes.data(), bytes.size()};
    }

    std::shared_ptr<PortState> state;
    FrameKind kind;
};

Frame::Frame(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
Frame::Frame(Frame&& other) noexcept = default;
Frame& Frame::operator=(Frame&& other) noexcept = default;
Frame::~Frame() = default;

Frame::Impl& Frame::impl() {
    if (!impl_) {
        throw std::runtime_error("frame is empty");
    }
    return *impl_;
}

const Frame::Impl& Frame::impl() const {
    if (!impl_) {
        throw std::runtime_error("frame is empty");
    }
    return *impl_;
}

#define UESTCRADAR_DEFINE_FRAME(FrameType, MetadataType, ElementType, Rows, Columns, MetadataBytes) \
    FrameType::FrameType(std::unique_ptr<Frame::Impl> value) noexcept                       \
        : Frame(std::move(value)) {}                                                        \
    FrameType::FrameType(FrameType&& other) noexcept = default;                            \
    FrameType& FrameType::operator=(FrameType&& other) noexcept = default;                 \
    FrameType::~FrameType() = default;                                                      \
    MetadataType FrameType::metadata() const {                                              \
        const MetadataType value = load_metadata<MetadataType>(impl().payload());           \
        if (payload_bytes(value) != impl().payload().size()) {                              \
            throw std::invalid_argument("frame metadata does not match its payload");      \
        }                                                                                   \
        return value;                                                                       \
    }                                                                                       \
    Array2D<ElementType> FrameType::data() {                                                 \
        const MetadataType value = metadata();                                              \
        return {reinterpret_cast<ElementType*>(impl().payload().data() + (MetadataBytes)),  \
                static_cast<std::size_t>(value.Rows),                                       \
                static_cast<std::size_t>(value.Columns)};                                   \
    }                                                                                       \
    Array2D<const ElementType> FrameType::data() const {                                     \
        const MetadataType value = metadata();                                              \
        return {reinterpret_cast<const ElementType*>(                                       \
                    impl().payload().data() + (MetadataBytes)),                             \
                static_cast<std::size_t>(value.Rows),                                       \
                static_cast<std::size_t>(value.Columns)};                                   \
    }

UESTCRADAR_DEFINE_FRAME(
    IQFrame,
    IQMetadata,
    ComplexInt16,
    channel_count,
    samples_per_channel,
    sizeof(IQMetadata))
UESTCRADAR_DEFINE_FRAME(
    PulseCompressionFrame,
    PulseCompressionMetadata,
    ComplexFloat32,
    channel_count,
    range_bin_count,
    sizeof(PulseCompressionMetadata))
UESTCRADAR_DEFINE_FRAME(
    RDFrame,
    RDMetadata,
    float,
    range_bin_count,
    doppler_bin_count,
    sizeof(RDWireMetadata))

#undef UESTCRADAR_DEFINE_FRAME

#define UESTCRADAR_DEFINE_INPUT(FrameType, MetadataType, Kind, TypeId)                \
    struct Input<FrameType>::Impl {                                                   \
        Impl() : state(open_port(                                                     \
            "UESTCRADAR_UPSTREAM_SHM_NAME", kUpstreamBufName, (TypeId))) {}          \
        std::shared_ptr<PortState> state;                                             \
    };                                                                                \
    Input<FrameType>::Input() : impl_(std::make_unique<Impl>()) {}                    \
    Input<FrameType>::Input(Input&& other) noexcept = default;                        \
    Input<FrameType>& Input<FrameType>::operator=(Input&& other) noexcept = default;   \
    Input<FrameType>::~Input() = default;                                              \
    FrameType Input<FrameType>::read() {                                               \
        if (!impl_) {                                                                  \
            throw std::runtime_error("input port is not open");                       \
        }                                                                              \
        PortState& state = *impl_->state;                                               \
        if (state.kind != LeaseKind::none) {                                            \
            throw std::runtime_error("the previous input frame is still alive");      \
        }                                                                              \
        wait_for_read(state);                                                           \
        try {                                                                          \
            const auto bytes = state.read_lease.payload();                              \
            const Envelope& envelope = state.read_lease.envelope();                    \
            if (bytes.size() < sizeof(MetadataType) ||                                 \
                envelope.type_id != (TypeId) ||                                        \
                envelope.type_version != kContractVersion) {                           \
                throw std::invalid_argument("input frame contract is invalid");        \
            }                                                                          \
            state.kind = LeaseKind::read;                                               \
            auto frame_impl = std::make_unique<Frame::Impl>(impl_->state, (Kind));      \
            FrameType frame{std::move(frame_impl)};                                     \
            const auto metadata = frame.metadata();                                    \
            validate_envelope(                                                         \
                state.read_lease.envelope(), *state.ring, (Kind), payload_bytes(metadata)); \
            return frame;                                                              \
        } catch (...) {                                                                 \
            if (state.kind == LeaseKind::read) {                                        \
                state.kind = LeaseKind::none;                                           \
            }                                                                          \
            if (state.read_lease.active()) {                                            \
                static_cast<void>(ringbuf_release(state.read_lease));                   \
            }                                                                          \
            throw;                                                                     \
        }                                                                              \
    }

UESTCRADAR_DEFINE_INPUT(IQFrame, IQMetadata, FrameKind::iq, kIQTypeId)
UESTCRADAR_DEFINE_INPUT(
    PulseCompressionFrame,
    PulseCompressionMetadata,
    FrameKind::pulse_compression,
    kPulseCompressionTypeId)
UESTCRADAR_DEFINE_INPUT(RDFrame, RDMetadata, FrameKind::rd, kRDTypeId)

#undef UESTCRADAR_DEFINE_INPUT

#define UESTCRADAR_DEFINE_OUTPUT(FrameType, MetadataType, Kind, TypeId)                 \
    struct Output<FrameType>::Impl {                                                    \
        Impl() : state(open_port(                                                       \
            "UESTCRADAR_DOWNSTREAM_SHM_NAME", kDownstreamBufName, (TypeId))) {}        \
        std::shared_ptr<PortState> state;                                               \
        std::uint64_t sequence{0};                                                      \
    };                                                                                 \
    Output<FrameType>::Output() : impl_(std::make_unique<Impl>()) {}                    \
    Output<FrameType>::Output(Output&& other) noexcept = default;                       \
    Output<FrameType>& Output<FrameType>::operator=(Output&& other) noexcept = default;  \
    Output<FrameType>::~Output() = default;                                              \
    FrameType Output<FrameType>::create(const MetadataType& metadata) {                 \
        if (!impl_) {                                                                   \
            throw std::runtime_error("output port is not open");                       \
        }                                                                               \
        const std::size_t length = payload_bytes(metadata);                             \
        const Envelope envelope = make_envelope(                                       \
            (Kind), length, ++impl_->sequence, unix_time_ns());                         \
        begin_output(*impl_->state, envelope, (Kind), length);                          \
        try {                                                                           \
            store_metadata(impl_->state->write_lease.payload(), metadata);              \
            return FrameType{                                                           \
                std::make_unique<Frame::Impl>(impl_->state, (Kind))};                   \
        } catch (...) {                                                                  \
            ringbuf_cancel(impl_->state->write_lease);                                  \
            impl_->state->kind = LeaseKind::none;                                       \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
    FrameType Output<FrameType>::create(                                                \
        const MetadataType& metadata, const Frame& parent) {                            \
        if (!impl_) {                                                                   \
            throw std::runtime_error("output port is not open");                       \
        }                                                                               \
        const Envelope& parent_envelope = parent.impl().envelope();                     \
        const std::size_t length = payload_bytes(metadata);                             \
        const Envelope envelope = make_envelope(                                       \
            (Kind), length, parent_envelope.frame_id, parent_envelope.timestamp);       \
        begin_output(*impl_->state, envelope, (Kind), length);                          \
        try {                                                                           \
            store_metadata(impl_->state->write_lease.payload(), metadata);              \
            return FrameType{                                                           \
                std::make_unique<Frame::Impl>(impl_->state, (Kind))};                   \
        } catch (...) {                                                                  \
            ringbuf_cancel(impl_->state->write_lease);                                  \
            impl_->state->kind = LeaseKind::none;                                       \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
    void Output<FrameType>::write(FrameType&& frame) {                                  \
        if (!impl_ || !frame.impl_ ||                                                   \
            frame.impl_->state.get() != impl_->state.get()) {                           \
            throw std::invalid_argument("output frame does not belong to this port");  \
        }                                                                               \
        PortState& state = *impl_->state;                                                \
        if (state.kind != LeaseKind::write) {                                            \
            throw std::runtime_error("output frame is not writable");                  \
        }                                                                               \
        const auto metadata = frame.metadata();                                         \
        validate_envelope(                                                              \
            state.write_lease.envelope(), *state.ring, (Kind), payload_bytes(metadata)); \
        if (ringbuf_commit(state.write_lease) != RingResult::ok) {                      \
            throw std::runtime_error("could not commit output frame");                 \
        }                                                                               \
        state.kind = LeaseKind::none;                                                    \
        frame.impl_->state.reset();                                                      \
        frame.impl_.reset();                                                             \
    }

UESTCRADAR_DEFINE_OUTPUT(IQFrame, IQMetadata, FrameKind::iq, kIQTypeId)
UESTCRADAR_DEFINE_OUTPUT(
    PulseCompressionFrame,
    PulseCompressionMetadata,
    FrameKind::pulse_compression,
    kPulseCompressionTypeId)
UESTCRADAR_DEFINE_OUTPUT(RDFrame, RDMetadata, FrameKind::rd, kRDTypeId)

#undef UESTCRADAR_DEFINE_OUTPUT

}  // namespace uestcradar
