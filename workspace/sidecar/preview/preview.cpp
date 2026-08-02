#include "preview.hpp"

#include "preview_contracts.generated.hpp"
#include "preview_generated.h"
#include "raw_frame.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace sidecar::preview {
namespace {

namespace fb = uestcradar::preview;
namespace contracts = uestcradar::preview_contracts;

constexpr std::size_t kWaveformBucket = 128;
constexpr std::size_t kHeatmapPool = 8;
constexpr std::size_t kMaxWireBytes = 8 * 1024 * 1024;
constexpr std::uint32_t kMaxAggregateMilliFps = 30'000;
constexpr auto kReconnectDelay = std::chrono::milliseconds{250};

std::uint64_t monotonic_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

const contracts::Contract* find_contract(
    std::uint64_t type_id,
    std::uint32_t type_version) noexcept {
    const auto found = std::find_if(
        contracts::catalog.begin(), contracts::catalog.end(),
        [&](const contracts::Contract& contract) {
            return contract.type_id == type_id &&
                   contract.type_version == type_version;
        });
    return found == contracts::catalog.end() ? nullptr : &*found;
}

std::uint64_t load_dimension(
    std::span<const std::byte> metadata,
    std::uint32_t offset,
    contracts::Scalar type) {
    const auto require = [&](std::size_t bytes) {
        if (offset > metadata.size() || bytes > metadata.size() - offset) {
            throw std::invalid_argument("preview metadata dimension is truncated");
        }
    };
    switch (type) {
        case contracts::Scalar::uint32: {
            require(sizeof(std::uint32_t));
            std::uint32_t value{};
            std::memcpy(&value, metadata.data() + offset, sizeof(value));
            return value;
        }
        case contracts::Scalar::uint64: {
            require(sizeof(std::uint64_t));
            std::uint64_t value{};
            std::memcpy(&value, metadata.data() + offset, sizeof(value));
            return value;
        }
        default:
            throw std::invalid_argument(
                "preview matrix dimensions must be unsigned integers");
    }
}

std::uint16_t float_to_half(float value) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16U) & 0x8000U;
    const std::uint32_t exponent = (bits >> 23U) & 0xffU;
    const std::uint32_t mantissa = bits & 0x7fffffU;
    if (exponent == 0xffU) {
        return static_cast<std::uint16_t>(
            sign | 0x7c00U | (mantissa == 0 ? 0U : 0x0200U));
    }
    const int adjusted = static_cast<int>(exponent) - 127 + 15;
    if (adjusted >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7bffU);
    }
    if (adjusted <= 0) {
        if (adjusted < -10) {
            return static_cast<std::uint16_t>(sign);
        }
        const std::uint32_t normalized = mantissa | 0x800000U;
        const int shift = 14 - adjusted;
        const std::uint32_t rounded =
            (normalized + (1U << (shift - 1))) >> shift;
        return static_cast<std::uint16_t>(sign | rounded);
    }
    const std::uint32_t rounded = mantissa + 0x1000U;
    if ((rounded & 0x800000U) != 0) {
        if (adjusted + 1 >= 31) {
            return static_cast<std::uint16_t>(sign | 0x7bffU);
        }
        return static_cast<std::uint16_t>(
            sign | (static_cast<std::uint32_t>(adjusted + 1) << 10U));
    }
    return static_cast<std::uint16_t>(
        sign | (static_cast<std::uint32_t>(adjusted) << 10U) |
        (rounded >> 13U));
}

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

struct ComplexI16 {
    std::int16_t i;
    std::int16_t q;
};

struct ComplexF32 {
    float i;
    float q;
};

static_assert(sizeof(ComplexI16) == 4);
static_assert(sizeof(ComplexF32) == 8);

std::uint64_t magnitude_squared(ComplexI16 value) noexcept {
    const std::int64_t i = value.i;
    const std::int64_t q = value.q;
    return static_cast<std::uint64_t>(i * i + q * q);
}

double magnitude_squared(ComplexF32 value) noexcept {
    return static_cast<double>(value.i) * value.i +
           static_cast<double>(value.q) * value.q;
}

struct EncodedBody {
    fb::PreviewBody type{fb::PreviewBody::NONE};
    flatbuffers::Offset<void> offset{};
    fb::ValueEncoding encoding{fb::ValueEncoding::Unknown};
    std::uint32_t pool_rows{0};
    std::uint32_t pool_columns{0};
};

EncodedBody encode_iq_waveform(
    flatbuffers::FlatBufferBuilder& builder,
    std::span<const std::byte> matrix,
    std::uint32_t channels,
    std::uint32_t columns) {
    const auto* values = reinterpret_cast<const ComplexI16*>(matrix.data());
    std::vector<flatbuffers::Offset<fb::WaveformChannel>> encoded_channels;
    encoded_channels.reserve(channels);
    const std::size_t buckets =
        (static_cast<std::size_t>(columns) + kWaveformBucket - 1) /
        kWaveformBucket;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        const ComplexI16* row =
            values + static_cast<std::size_t>(channel) * columns;
        std::int32_t peak = 0;
        for (std::uint32_t column = 0; column < columns; ++column) {
            peak = std::max(peak, std::abs(static_cast<int>(row[column].i)));
            peak = std::max(peak, std::abs(static_cast<int>(row[column].q)));
        }
        const float scale = peak == 0 ? 1.0f : static_cast<float>(peak) / 127.0f;
        std::vector<std::uint8_t> min_offsets;
        std::vector<std::uint8_t> max_offsets;
        std::vector<std::uint8_t> packed;
        min_offsets.reserve(buckets);
        max_offsets.reserve(buckets);
        packed.reserve(buckets * 4);
        for (std::size_t bucket = 0; bucket < buckets; ++bucket) {
            const std::size_t begin = bucket * kWaveformBucket;
            const std::size_t end = std::min<std::size_t>(columns, begin + kWaveformBucket);
            std::size_t minimum = begin;
            std::size_t maximum = begin;
            for (std::size_t index = begin + 1; index < end; ++index) {
                if (magnitude_squared(row[index]) < magnitude_squared(row[minimum])) {
                    minimum = index;
                }
                if (magnitude_squared(row[index]) > magnitude_squared(row[maximum])) {
                    maximum = index;
                }
            }
            min_offsets.push_back(static_cast<std::uint8_t>(minimum - begin));
            max_offsets.push_back(static_cast<std::uint8_t>(maximum - begin));
            const auto quantize = [&](std::int16_t sample) {
                return static_cast<std::uint8_t>(static_cast<std::int8_t>(
                    std::clamp(std::lround(static_cast<float>(sample) / scale),
                               -127L, 127L)));
            };
            packed.push_back(quantize(row[minimum].i));
            packed.push_back(quantize(row[minimum].q));
            packed.push_back(quantize(row[maximum].i));
            packed.push_back(quantize(row[maximum].q));
        }
        encoded_channels.push_back(fb::CreateWaveformChannelDirect(
            builder, channel, static_cast<std::uint32_t>(buckets), scale,
            &min_offsets, &max_offsets, &packed));
    }
    const auto waveform = fb::CreateWaveformPreviewDirect(
        builder, &encoded_channels);
    return {
        fb::PreviewBody::WaveformPreview,
        waveform.Union(),
        fb::ValueEncoding::ComplexInt8,
        channels,
        static_cast<std::uint32_t>(buckets),
    };
}

EncodedBody encode_float_waveform(
    flatbuffers::FlatBufferBuilder& builder,
    std::span<const std::byte> matrix,
    std::uint32_t channels,
    std::uint32_t columns) {
    const auto* values = reinterpret_cast<const ComplexF32*>(matrix.data());
    std::vector<flatbuffers::Offset<fb::WaveformChannel>> encoded_channels;
    encoded_channels.reserve(channels);
    const std::size_t buckets =
        (static_cast<std::size_t>(columns) + kWaveformBucket - 1) /
        kWaveformBucket;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        const ComplexF32* row =
            values + static_cast<std::size_t>(channel) * columns;
        std::vector<std::uint8_t> min_offsets;
        std::vector<std::uint8_t> max_offsets;
        std::vector<std::uint8_t> packed;
        min_offsets.reserve(buckets);
        max_offsets.reserve(buckets);
        packed.reserve(buckets * 8);
        for (std::size_t bucket = 0; bucket < buckets; ++bucket) {
            const std::size_t begin = bucket * kWaveformBucket;
            const std::size_t end = std::min<std::size_t>(columns, begin + kWaveformBucket);
            std::size_t minimum = begin;
            std::size_t maximum = begin;
            for (std::size_t index = begin + 1; index < end; ++index) {
                const double candidate = magnitude_squared(row[index]);
                if (std::isfinite(candidate) &&
                    (!std::isfinite(magnitude_squared(row[minimum])) ||
                     candidate < magnitude_squared(row[minimum]))) {
                    minimum = index;
                }
                if (std::isfinite(candidate) &&
                    (!std::isfinite(magnitude_squared(row[maximum])) ||
                     candidate > magnitude_squared(row[maximum]))) {
                    maximum = index;
                }
            }
            min_offsets.push_back(static_cast<std::uint8_t>(minimum - begin));
            max_offsets.push_back(static_cast<std::uint8_t>(maximum - begin));
            append_u16(packed, float_to_half(row[minimum].i));
            append_u16(packed, float_to_half(row[minimum].q));
            append_u16(packed, float_to_half(row[maximum].i));
            append_u16(packed, float_to_half(row[maximum].q));
        }
        encoded_channels.push_back(fb::CreateWaveformChannelDirect(
            builder, channel, static_cast<std::uint32_t>(buckets), 1.0f,
            &min_offsets, &max_offsets, &packed));
    }
    const auto waveform = fb::CreateWaveformPreviewDirect(
        builder, &encoded_channels);
    return {
        fb::PreviewBody::WaveformPreview,
        waveform.Union(),
        fb::ValueEncoding::ComplexFloat16,
        channels,
        static_cast<std::uint32_t>(buckets),
    };
}

EncodedBody encode_heatmap(
    flatbuffers::FlatBufferBuilder& builder,
    std::span<const std::byte> matrix,
    std::uint32_t rows,
    std::uint32_t columns,
    std::uint32_t channel_index) {
    const auto* values = reinterpret_cast<const float*>(matrix.data());
    const std::uint32_t output_rows =
        (rows + static_cast<std::uint32_t>(kHeatmapPool) - 1U) /
        static_cast<std::uint32_t>(kHeatmapPool);
    const std::uint32_t output_columns =
        (columns + static_cast<std::uint32_t>(kHeatmapPool) - 1U) /
        static_cast<std::uint32_t>(kHeatmapPool);
    std::vector<std::uint8_t> offsets;
    std::vector<std::uint8_t> packed;
    offsets.reserve(static_cast<std::size_t>(output_rows) * output_columns);
    packed.reserve(static_cast<std::size_t>(output_rows) * output_columns * 2);
    for (std::uint32_t output_row = 0; output_row < output_rows; ++output_row) {
        for (std::uint32_t output_column = 0; output_column < output_columns; ++output_column) {
            const std::uint32_t begin_row = output_row * kHeatmapPool;
            const std::uint32_t begin_column = output_column * kHeatmapPool;
            const std::uint32_t end_row = std::min<std::uint32_t>(rows, begin_row + kHeatmapPool);
            const std::uint32_t end_column = std::min<std::uint32_t>(columns, begin_column + kHeatmapPool);
            std::uint32_t best_row = begin_row;
            std::uint32_t best_column = begin_column;
            float best = values[static_cast<std::size_t>(best_row) * columns + best_column];
            for (std::uint32_t row = begin_row; row < end_row; ++row) {
                for (std::uint32_t column = begin_column; column < end_column; ++column) {
                    const float candidate = values[static_cast<std::size_t>(row) * columns + column];
                    if (std::isfinite(candidate) &&
                        (!std::isfinite(best) || std::abs(candidate) > std::abs(best))) {
                        best = candidate;
                        best_row = row;
                        best_column = column;
                    }
                }
            }
            offsets.push_back(static_cast<std::uint8_t>(
                (best_row - begin_row) * kHeatmapPool +
                (best_column - begin_column)));
            append_u16(packed, float_to_half(best));
        }
    }
    const auto heatmap = fb::CreateHeatmapPreviewDirect(
        builder, channel_index, output_rows, output_columns,
        &offsets, &packed);
    return {
        fb::PreviewBody::HeatmapPreview,
        heatmap.Union(),
        fb::ValueEncoding::Float16,
        output_rows,
        output_columns,
    };
}

std::vector<std::uint8_t> encode_frame(
    std::span<const std::byte> frame,
    Leg leg,
    std::string_view node_id,
    std::string_view instance_id) {
    if (frame.size() < uestcradar::kEnvelopeSize) {
        throw std::invalid_argument("preview RawFrame is truncated");
    }
    uestcradar::Envelope envelope{};
    std::memcpy(&envelope, frame.data(), sizeof(envelope));
    if (envelope.payload_length != frame.size() - sizeof(envelope)) {
        throw std::invalid_argument("preview RawFrame length mismatch");
    }
    const contracts::Contract* contract = find_contract(
        envelope.type_id, envelope.type_version);
    if (contract == nullptr || envelope.payload_length < contract->metadata_bytes) {
        throw std::invalid_argument("unsupported preview frame contract");
    }
    const auto payload = frame.subspan(sizeof(envelope));
    const auto metadata = payload.first(contract->metadata_bytes);
    const std::uint64_t rows64 = load_dimension(
        metadata, contract->rows_offset, contract->rows_type);
    const std::uint64_t columns64 = load_dimension(
        metadata, contract->columns_offset, contract->columns_type);
    if (rows64 == 0 || columns64 == 0 || rows64 > UINT32_MAX ||
        columns64 > UINT32_MAX || rows64 > SIZE_MAX / columns64 ||
        rows64 * columns64 >
            (SIZE_MAX - contract->metadata_bytes) / contract->element_bytes) {
        throw std::invalid_argument("preview matrix dimensions are invalid");
    }
    const std::size_t expected = contract->metadata_bytes +
        static_cast<std::size_t>(rows64 * columns64) * contract->element_bytes;
    if (expected != payload.size()) {
        throw std::invalid_argument("preview matrix length mismatch");
    }
    const auto matrix = payload.subspan(contract->metadata_bytes);
    const auto rows = static_cast<std::uint32_t>(rows64);
    const auto columns = static_cast<std::uint32_t>(columns64);
    std::uint32_t channel_index = 0;
    if (contract->channel_index_offset != contracts::no_channel_index) {
        channel_index = static_cast<std::uint32_t>(load_dimension(
            metadata, contract->channel_index_offset,
            contracts::Scalar::uint32));
    }

    flatbuffers::FlatBufferBuilder builder{1024};
    EncodedBody body;
    if (contract->visualization == contracts::Visualization::waveform &&
        contract->element == contracts::Element::complex_int16) {
        body = encode_iq_waveform(builder, matrix, rows, columns);
    } else if (
        contract->visualization == contracts::Visualization::waveform &&
        contract->element == contracts::Element::complex_float32) {
        body = encode_float_waveform(builder, matrix, rows, columns);
    } else if (
        contract->visualization == contracts::Visualization::heatmap &&
        contract->element == contracts::Element::float32) {
        body = encode_heatmap(
            builder, matrix, rows, columns, channel_index);
    } else {
        throw std::invalid_argument("unsupported preview visualization");
    }

    const auto node = builder.CreateString(node_id.data(), node_id.size());
    const auto instance = builder.CreateString(
        instance_id.data(), instance_id.size());
    const auto metadata_vector = builder.CreateVector(
        reinterpret_cast<const std::uint8_t*>(metadata.data()),
        metadata.size());
    const auto preview_frame = fb::CreatePreviewFrame(
        builder,
        node,
        instance,
        leg == Leg::input ? fb::Leg::Input : fb::Leg::Output,
        envelope.type_id,
        envelope.type_version,
        envelope.frame_id,
        envelope.timestamp,
        rows,
        columns,
        body.pool_rows,
        body.pool_columns,
        body.encoding,
        metadata_vector,
        body.type,
        body.offset);
    const auto message = fb::CreatePreviewMessage(
        builder, 1, fb::MessagePayload::PreviewFrame,
        preview_frame.Union());
    fb::FinishPreviewMessageBuffer(builder, message);
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

bool send_all(int descriptor, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t sent = 0;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds{250};
    while (sent < size) {
        const ssize_t result = ::send(
            descriptor, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (result > 0) {
            sent += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) {
                return false;
            }
            pollfd item{descriptor, POLLOUT, 0};
            if (::poll(&item, 1, static_cast<int>(remaining.count())) <= 0) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

bool send_message(int descriptor, std::span<const std::uint8_t> message) noexcept {
    if (message.empty() || message.size() > kMaxWireBytes) {
        return false;
    }
    const std::uint32_t length = htonl(static_cast<std::uint32_t>(message.size()));
    return send_all(descriptor, &length, sizeof(length)) &&
           send_all(descriptor, message.data(), message.size());
}

int connect_tcp(const std::string& host, std::uint16_t port) noexcept {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) {
        return -1;
    }
    int connected = -1;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        const int descriptor = ::socket(
            address->ai_family, address->ai_socktype | SOCK_NONBLOCK,
            address->ai_protocol);
        if (descriptor < 0) {
            continue;
        }
        const int result = ::connect(
            descriptor, address->ai_addr, address->ai_addrlen);
        if (result == 0 || errno == EINPROGRESS) {
            pollfd item{descriptor, POLLOUT, 0};
            if (result == 0 || ::poll(&item, 1, 2'000) > 0) {
                int error = 0;
                socklen_t length = sizeof(error);
                if (::getsockopt(
                        descriptor, SOL_SOCKET, SO_ERROR,
                        &error, &length) == 0 && error == 0) {
                    connected = descriptor;
                    break;
                }
            }
        }
        ::close(descriptor);
    }
    ::freeaddrinfo(addresses);
    return connected;
}

std::vector<std::uint8_t> hello_message(const Config& config) {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<fb::StreamDescriptor>> streams;
    if (config.input.enabled) {
        streams.push_back(fb::CreateStreamDescriptor(
            builder, fb::Leg::Input, config.input.type_id,
            config.input.type_version));
    }
    if (config.output.enabled) {
        streams.push_back(fb::CreateStreamDescriptor(
            builder, fb::Leg::Output, config.output.type_id,
            config.output.type_version));
    }
    const auto node = builder.CreateString(config.node_id);
    const auto instance = builder.CreateString(config.instance_id);
    const auto stream_vector = builder.CreateVector(streams);
    const auto hello = fb::CreateSidecarHello(
        builder, node, instance, stream_vector);
    const auto message = fb::CreatePreviewMessage(
        builder, 1, fb::MessagePayload::SidecarHello, hello.Union());
    fb::FinishPreviewMessageBuffer(builder, message);
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<std::uint8_t> status_message(
    const Config& config,
    Leg leg,
    float actual_fps,
    const Counters& counters) {
    const StreamConfig& stream =
        leg == Leg::input ? config.input : config.output;
    flatbuffers::FlatBufferBuilder builder;
    const auto node = builder.CreateString(config.node_id);
    const auto status = fb::CreateStreamStatus(
        builder,
        node,
        leg == Leg::input ? fb::Leg::Input : fb::Leg::Output,
        stream.type_id,
        stream.type_version,
        actual_fps,
        counters.snapshot_drops,
        counters.encode_drops,
        counters.network_drops);
    const auto message = fb::CreatePreviewMessage(
        builder, 1, fb::MessagePayload::StreamStatus, status.Union());
    fb::FinishPreviewMessageBuffer(builder, message);
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

class SnapshotTap final : public forwarder::FrameTap {
public:
    explicit SnapshotTap(std::size_t capacity)
        : storage_(capacity) {}

    void set_rate(std::uint32_t milli_fps) noexcept {
        rate_milli_fps_.store(milli_fps, std::memory_order_release);
        if (milli_fps == 0) {
            next_capture_ns_.store(0, std::memory_order_relaxed);
            std::uint8_t ready = 2;
            static_cast<void>(state_.compare_exchange_strong(
                ready, 0, std::memory_order_release,
                std::memory_order_relaxed));
        }
    }

    std::uint32_t rate() const noexcept {
        return rate_milli_fps_.load(std::memory_order_acquire);
    }

    void try_capture(std::span<const std::byte> frame) noexcept override {
        const std::uint32_t rate = rate_milli_fps_.load(
            std::memory_order_relaxed);
        if (rate == 0) {
            return;
        }
        const std::uint64_t now = monotonic_ns();
        std::uint64_t next = next_capture_ns_.load(std::memory_order_relaxed);
        if (now < next) {
            return;
        }
        const std::uint64_t interval = 1'000'000'000'000ULL / rate;
        if (!next_capture_ns_.compare_exchange_strong(
                next, now + interval,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return;
        }
        std::uint8_t expected = 0;
        if (frame.size() > storage_.size() ||
            !state_.compare_exchange_strong(
                expected, 1, std::memory_order_acquire,
                std::memory_order_relaxed)) {
            snapshot_drops_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        std::memcpy(storage_.data(), frame.data(), frame.size());
        length_ = frame.size();
        captured_.fetch_add(1, std::memory_order_relaxed);
        state_.store(2, std::memory_order_release);
    }

    bool acquire(std::span<const std::byte>& frame) noexcept {
        std::uint8_t expected = 2;
        if (!state_.compare_exchange_strong(
                expected, 3, std::memory_order_acquire,
                std::memory_order_relaxed)) {
            return false;
        }
        frame = {storage_.data(), length_};
        return true;
    }

    void release() noexcept {
        state_.store(0, std::memory_order_release);
    }

    Counters counters() const noexcept {
        return {
            captured_.load(std::memory_order_relaxed),
            snapshot_drops_.load(std::memory_order_relaxed),
            encode_drops_.load(std::memory_order_relaxed),
            network_drops_.load(std::memory_order_relaxed),
        };
    }

    void encode_drop() noexcept {
        encode_drops_.fetch_add(1, std::memory_order_relaxed);
    }

    void network_drop() noexcept {
        network_drops_.fetch_add(1, std::memory_order_relaxed);
    }

    void sent() noexcept {
        sent_.fetch_add(1, std::memory_order_relaxed);
    }

    std::uint64_t sent_count() const noexcept {
        return sent_.load(std::memory_order_relaxed);
    }

private:
    std::vector<std::byte> storage_;
    std::atomic<std::uint8_t> state_{0};
    std::atomic<std::uint32_t> rate_milli_fps_{0};
    std::atomic<std::uint64_t> next_capture_ns_{0};
    std::size_t length_{0};
    std::atomic<std::uint64_t> captured_{0};
    std::atomic<std::uint64_t> snapshot_drops_{0};
    std::atomic<std::uint64_t> encode_drops_{0};
    std::atomic<std::uint64_t> network_drops_{0};
    std::atomic<std::uint64_t> sent_{0};
};

}  // namespace

class Runtime::Impl {
public:
    explicit Impl(Config value)
        : config(std::move(value)),
          input(config.input.enabled ? config.input.max_frame_bytes : 0),
          output(config.output.enabled ? config.output.max_frame_bytes : 0) {}

    void clear_rates() noexcept {
        input.set_rate(0);
        output.set_rate(0);
    }

    void apply_subscription(const fb::SubscriptionUpdate& update) noexcept {
        std::uint32_t input_rate = 0;
        std::uint32_t output_rate = 0;
        if (const auto* selectors = update.selectors()) {
            for (const fb::StreamSelector* selector : *selectors) {
                if (selector == nullptr || selector->node_id() == nullptr ||
                    selector->node_id()->str() != config.node_id) {
                    continue;
                }
                const float requested = std::clamp(
                    selector->requested_fps(), 0.0f, 30.0f);
                const auto rate = static_cast<std::uint32_t>(requested * 1000.0f);
                if (selector->leg() == fb::Leg::Input && config.input.enabled &&
                    selector->frame_type_id() == config.input.type_id &&
                    selector->frame_type_version() == config.input.type_version) {
                    input_rate = std::max(input_rate, rate);
                }
                if (selector->leg() == fb::Leg::Output && config.output.enabled &&
                    selector->frame_type_id() == config.output.type_id &&
                    selector->frame_type_version() == config.output.type_version) {
                    output_rate = std::max(output_rate, rate);
                }
            }
        }
        const std::uint64_t total =
            static_cast<std::uint64_t>(input_rate) + output_rate;
        if (total > kMaxAggregateMilliFps) {
            input_rate = static_cast<std::uint32_t>(
                input_rate * kMaxAggregateMilliFps / total);
            output_rate = kMaxAggregateMilliFps - input_rate;
        }
        input.set_rate(input_rate);
        output.set_rate(output_rate);
    }

    void receive_control(int descriptor, std::vector<std::uint8_t>& input_bytes) noexcept {
        std::array<std::uint8_t, 4096> buffer{};
        for (;;) {
            const ssize_t count = ::recv(
                descriptor, buffer.data(), buffer.size(), MSG_DONTWAIT);
            if (count > 0) {
                input_bytes.insert(
                    input_bytes.end(), buffer.begin(), buffer.begin() + count);
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        while (input_bytes.size() >= sizeof(std::uint32_t)) {
            std::uint32_t network_length{};
            std::memcpy(&network_length, input_bytes.data(), sizeof(network_length));
            const std::size_t length = ntohl(network_length);
            if (length == 0 || length > kMaxWireBytes) {
                input_bytes.clear();
                clear_rates();
                return;
            }
            if (input_bytes.size() < sizeof(network_length) + length) {
                return;
            }
            const std::uint8_t* message_bytes =
                input_bytes.data() + sizeof(network_length);
            flatbuffers::Verifier verifier{message_bytes, length};
            if (fb::VerifyPreviewMessageBuffer(verifier)) {
                const fb::PreviewMessage* message =
                    fb::GetPreviewMessage(message_bytes);
                if (message->protocol_version() == 1 &&
                    message->payload_type() == fb::MessagePayload::SubscriptionUpdate) {
                    if (const auto* update = message->payload_as_SubscriptionUpdate()) {
                        apply_subscription(*update);
                    }
                }
            }
            input_bytes.erase(
                input_bytes.begin(),
                input_bytes.begin() + sizeof(network_length) + length);
        }
    }

    bool process(
        int descriptor,
        SnapshotTap& tap,
        Leg leg) noexcept {
        std::span<const std::byte> frame;
        if (!tap.acquire(frame)) {
            return true;
        }
        if (tap.rate() == 0) {
            tap.release();
            return true;
        }
        bool success = true;
        try {
            const auto encoded = encode_frame(
                frame, leg, config.node_id, config.instance_id);
            if (!send_message(descriptor, encoded)) {
                tap.network_drop();
                success = false;
            } else {
                tap.sent();
            }
        } catch (const std::exception&) {
            tap.encode_drop();
        }
        tap.release();
        return success;
    }

    Config config;
    SnapshotTap input;
    SnapshotTap output;
};

Runtime::Runtime(Config config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

Runtime::~Runtime() = default;

forwarder::FrameTap* Runtime::input_tap() noexcept {
    return impl_->config.input.enabled ? &impl_->input : nullptr;
}

forwarder::FrameTap* Runtime::output_tap() noexcept {
    return impl_->config.output.enabled ? &impl_->output : nullptr;
}

Counters Runtime::counters(Leg leg) const noexcept {
    return leg == Leg::input
               ? impl_->input.counters()
               : impl_->output.counters();
}

void Runtime::run(volatile std::sig_atomic_t& running) noexcept {
    if (impl_->config.host.empty()) {
        return;
    }
    const auto hello = hello_message(impl_->config);
    while (running != 0) {
        const int descriptor = connect_tcp(
            impl_->config.host, impl_->config.port);
        if (descriptor < 0) {
            std::this_thread::sleep_for(kReconnectDelay);
            continue;
        }
        std::cout << "sidecar: preview connected host="
                  << impl_->config.host << ':' << impl_->config.port
                  << std::endl;
        std::vector<std::uint8_t> incoming;
        bool connected = send_message(descriptor, hello);
        auto next_status = std::chrono::steady_clock::now() +
            std::chrono::seconds{1};
        std::uint64_t previous_input_sent = impl_->input.sent_count();
        std::uint64_t previous_output_sent = impl_->output.sent_count();
        while (running != 0 && connected) {
            impl_->receive_control(descriptor, incoming);
            connected = impl_->process(
                            descriptor, impl_->input, Leg::input) &&
                        impl_->process(
                            descriptor, impl_->output, Leg::output);
            const auto now = std::chrono::steady_clock::now();
            if (connected && now >= next_status) {
                const std::uint64_t input_sent = impl_->input.sent_count();
                const std::uint64_t output_sent = impl_->output.sent_count();
                if (impl_->config.input.enabled) {
                    connected = send_message(descriptor, status_message(
                        impl_->config, Leg::input,
                        std::min(30.0F, static_cast<float>(
                            input_sent - previous_input_sent)),
                        impl_->input.counters()));
                }
                if (connected && impl_->config.output.enabled) {
                    connected = send_message(descriptor, status_message(
                        impl_->config, Leg::output,
                        std::min(30.0F, static_cast<float>(
                            output_sent - previous_output_sent)),
                        impl_->output.counters()));
                }
                previous_input_sent = input_sent;
                previous_output_sent = output_sent;
                next_status = now + std::chrono::seconds{1};
            }
            if (connected) {
                pollfd item{descriptor, POLLIN, 0};
                const int result = ::poll(&item, 1, 2);
                if (result > 0 && (item.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                    connected = false;
                }
            }
        }
        impl_->clear_rates();
        ::close(descriptor);
        if (running != 0) {
            std::cerr << "sidecar: preview disconnected; retrying"
                      << std::endl;
        }
        if (running != 0) {
            std::this_thread::sleep_for(kReconnectDelay);
        }
    }
}

std::vector<std::uint8_t> encode_frame_for_test(
    std::span<const std::byte> frame,
    Leg leg,
    const std::string& node_id,
    const std::string& instance_id) {
    return encode_frame(frame, leg, node_id, instance_id);
}

}  // namespace sidecar::preview
