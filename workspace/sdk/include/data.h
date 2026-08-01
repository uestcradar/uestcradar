#pragma once

#include <sdk.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace uestcradar {

struct ComplexInt16 {
    std::int16_t i{};
    std::int16_t q{};
};

struct ComplexFloat32 {
    float i{};
    float q{};
};

template <class T>
class Array2D {
public:
    Array2D(T* values, std::size_t rows, std::size_t columns) noexcept
        : values_(values), rows_(rows), columns_(columns) {}

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }

    [[nodiscard]] std::span<T> operator[](std::size_t row) const {
        if (row >= rows_) {
            throw std::out_of_range("data row is out of range");
        }
        return {values_ + row * columns_, columns_};
    }

    [[nodiscard]] std::span<T> values() const noexcept {
        return {values_, rows_ * columns_};
    }

private:
    T* values_;
    std::size_t rows_;
    std::size_t columns_;
};

struct IQMetadata {
    std::uint32_t channel_count{};
    std::uint32_t samples_per_channel{};
    double sample_rate_hz{};
    double center_frequency_hz{};
};

struct PulseCompressionMetadata {
    std::uint32_t channel_count{};
    std::uint32_t range_bin_count{};
    std::uint32_t pulse_index{};
    std::uint32_t pulses_per_cpi{};
    double range_resolution_m{};
};

struct RDMetadata {
    std::uint32_t channel_index{};
    std::uint32_t range_bin_count{};
    std::uint32_t doppler_bin_count{};
    std::uint32_t reserved{};
    double range_resolution_m{};
    double velocity_resolution_mps{};
};

class IQFrameView {
public:
    static constexpr std::uint64_t type_id = 1;
    static constexpr std::uint32_t type_version = 2;

    [[nodiscard]] static std::size_t payload_bytes(
        const IQMetadata& metadata) {
        return checked_payload_bytes(
            metadata.channel_count, metadata.samples_per_channel);
    }

    [[nodiscard]] static IQFrameView from(RawFrame& frame) {
        validate(frame, sizeof(IQMetadata));
        const IQMetadata& metadata = metadata_of(frame);
        if (payload_bytes(metadata) != frame.payload_span().size()) {
            throw std::invalid_argument("IQ data length is invalid");
        }
        return IQFrameView{frame};
    }

    [[nodiscard]] static IQFrameView initialize(
        RawFrame& frame,
        const IQMetadata& metadata) {
        validate(frame, sizeof(IQMetadata));
        if (payload_bytes(metadata) != frame.payload_span().size()) {
            throw std::invalid_argument("IQ data capacity is invalid");
        }
        ::new (frame.payload_span().data()) IQMetadata{metadata};
        return IQFrameView{frame};
    }

    [[nodiscard]] IQMetadata& metadata() const noexcept {
        return metadata_of(*frame_);
    }

    [[nodiscard]] Array2D<ComplexInt16> data() const noexcept {
        return {
            reinterpret_cast<ComplexInt16*>(
                frame_->payload_span().data() + sizeof(IQMetadata)),
            metadata().channel_count,
            metadata().samples_per_channel};
    }

private:
    explicit IQFrameView(RawFrame& frame) noexcept : frame_(&frame) {}

    static std::size_t checked_payload_bytes(
        std::size_t rows,
        std::size_t columns) {
        if (rows == 0 || columns == 0 ||
            rows > std::numeric_limits<std::size_t>::max() / columns) {
            throw std::invalid_argument("IQ data dimensions are invalid");
        }
        const std::size_t elements = rows * columns;
        if (elements >
            (std::numeric_limits<std::size_t>::max() -
             sizeof(IQMetadata)) /
                sizeof(ComplexInt16)) {
            throw std::invalid_argument("IQ data size overflows");
        }
        return sizeof(IQMetadata) + elements * sizeof(ComplexInt16);
    }

    static void validate(RawFrame& frame, std::size_t minimum_payload) {
        const Envelope& envelope = frame.envelope();
        if (envelope.type_id != type_id ||
            envelope.type_version != type_version) {
            throw std::invalid_argument("RawFrame does not contain IQ data");
        }
        if (envelope.payload_length != frame.payload_span().size() ||
            envelope.payload_length < minimum_payload) {
            throw std::invalid_argument("IQ payload is truncated");
        }
    }

    static IQMetadata& metadata_of(RawFrame& frame) noexcept {
        return *reinterpret_cast<IQMetadata*>(frame.payload_span().data());
    }

    RawFrame* frame_;
};

class PulseCompressionFrameView {
public:
    static constexpr std::uint64_t type_id = 2;
    static constexpr std::uint32_t type_version = 2;

    [[nodiscard]] static std::size_t payload_bytes(
        const PulseCompressionMetadata& metadata) {
        return checked_payload_bytes(
            metadata.channel_count, metadata.range_bin_count);
    }

    [[nodiscard]] static PulseCompressionFrameView from(RawFrame& frame) {
        validate(frame, sizeof(PulseCompressionMetadata));
        const auto& metadata = metadata_of(frame);
        if (payload_bytes(metadata) != frame.payload_span().size()) {
            throw std::invalid_argument(
                "pulse compression data length is invalid");
        }
        return PulseCompressionFrameView{frame};
    }

    [[nodiscard]] static PulseCompressionFrameView initialize(
        RawFrame& frame,
        const PulseCompressionMetadata& metadata) {
        validate(frame, sizeof(PulseCompressionMetadata));
        if (payload_bytes(metadata) != frame.payload_span().size()) {
            throw std::invalid_argument(
                "pulse compression data capacity is invalid");
        }
        ::new (frame.payload_span().data())
            PulseCompressionMetadata{metadata};
        return PulseCompressionFrameView{frame};
    }

    [[nodiscard]] PulseCompressionMetadata& metadata() const noexcept {
        return metadata_of(*frame_);
    }

    [[nodiscard]] Array2D<ComplexFloat32> data() const noexcept {
        return {
            reinterpret_cast<ComplexFloat32*>(
                frame_->payload_span().data() +
                sizeof(PulseCompressionMetadata)),
            metadata().channel_count,
            metadata().range_bin_count};
    }

private:
    explicit PulseCompressionFrameView(RawFrame& frame) noexcept
        : frame_(&frame) {}

    static std::size_t checked_payload_bytes(
        std::size_t rows,
        std::size_t columns) {
        if (rows == 0 || columns == 0 ||
            rows > std::numeric_limits<std::size_t>::max() / columns) {
            throw std::invalid_argument(
                "pulse compression data dimensions are invalid");
        }
        const std::size_t elements = rows * columns;
        if (elements >
            (std::numeric_limits<std::size_t>::max() -
             sizeof(PulseCompressionMetadata)) /
                sizeof(ComplexFloat32)) {
            throw std::invalid_argument(
                "pulse compression data size overflows");
        }
        return sizeof(PulseCompressionMetadata) +
               elements * sizeof(ComplexFloat32);
    }

    static void validate(RawFrame& frame, std::size_t minimum_payload) {
        const Envelope& envelope = frame.envelope();
        if (envelope.type_id != type_id ||
            envelope.type_version != type_version) {
            throw std::invalid_argument(
                "RawFrame does not contain pulse compression data");
        }
        if (envelope.payload_length != frame.payload_span().size() ||
            envelope.payload_length < minimum_payload) {
            throw std::invalid_argument(
                "pulse compression payload is truncated");
        }
    }

    static PulseCompressionMetadata& metadata_of(
        RawFrame& frame) noexcept {
        return *reinterpret_cast<PulseCompressionMetadata*>(
            frame.payload_span().data());
    }

    RawFrame* frame_;
};

class RDFrameView {
public:
    static constexpr std::uint64_t type_id = 3;
    static constexpr std::uint32_t type_version = 2;

    [[nodiscard]] static std::size_t payload_bytes(
        const RDMetadata& metadata) {
        return checked_payload_bytes(
            metadata.range_bin_count, metadata.doppler_bin_count);
    }

    [[nodiscard]] static RDFrameView from(RawFrame& frame) {
        validate(frame, sizeof(RDMetadata));
        const RDMetadata& metadata = metadata_of(frame);
        if (metadata.reserved != 0 ||
            payload_bytes(metadata) != frame.payload_span().size()) {
            throw std::invalid_argument("RD data length is invalid");
        }
        return RDFrameView{frame};
    }

    [[nodiscard]] static RDFrameView initialize(
        RawFrame& frame,
        const RDMetadata& metadata) {
        validate(frame, sizeof(RDMetadata));
        if (metadata.reserved != 0 ||
            payload_bytes(metadata) != frame.payload_span().size()) {
            throw std::invalid_argument("RD data capacity is invalid");
        }
        ::new (frame.payload_span().data()) RDMetadata{metadata};
        return RDFrameView{frame};
    }

    [[nodiscard]] RDMetadata& metadata() const noexcept {
        return metadata_of(*frame_);
    }

    [[nodiscard]] Array2D<float> data() const noexcept {
        return {
            reinterpret_cast<float*>(
                frame_->payload_span().data() + sizeof(RDMetadata)),
            metadata().range_bin_count,
            metadata().doppler_bin_count};
    }

private:
    explicit RDFrameView(RawFrame& frame) noexcept : frame_(&frame) {}

    static std::size_t checked_payload_bytes(
        std::size_t rows,
        std::size_t columns) {
        if (rows == 0 || columns == 0 ||
            rows > std::numeric_limits<std::size_t>::max() / columns) {
            throw std::invalid_argument("RD data dimensions are invalid");
        }
        const std::size_t elements = rows * columns;
        if (elements >
            (std::numeric_limits<std::size_t>::max() -
             sizeof(RDMetadata)) /
                sizeof(float)) {
            throw std::invalid_argument("RD data size overflows");
        }
        return sizeof(RDMetadata) + elements * sizeof(float);
    }

    static void validate(RawFrame& frame, std::size_t minimum_payload) {
        const Envelope& envelope = frame.envelope();
        if (envelope.type_id != type_id ||
            envelope.type_version != type_version) {
            throw std::invalid_argument("RawFrame does not contain RD data");
        }
        if (envelope.payload_length != frame.payload_span().size() ||
            envelope.payload_length < minimum_payload) {
            throw std::invalid_argument("RD payload is truncated");
        }
    }

    static RDMetadata& metadata_of(RawFrame& frame) noexcept {
        return *reinterpret_cast<RDMetadata*>(frame.payload_span().data());
    }

    RawFrame* frame_;
};

static_assert(std::endian::native == std::endian::little);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(sizeof(ComplexInt16) == 4);
static_assert(sizeof(ComplexFloat32) == 8);
static_assert(sizeof(IQMetadata) == 24);
static_assert(sizeof(PulseCompressionMetadata) == 24);
static_assert(sizeof(RDMetadata) == 32);
static_assert(std::is_trivially_copyable_v<IQMetadata>);
static_assert(std::is_trivially_copyable_v<PulseCompressionMetadata>);
static_assert(std::is_trivially_copyable_v<RDMetadata>);

}  // namespace uestcradar
