#pragma once

#include <flowgraph/block.h>
#include <flowgraph/plugin.h>
#include <flowgraph/port.h>
#include <flowgraph/value.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace cycore::sdk {

class Params {
public:
    explicit Params(const cy::flowgraph::ValueMap& values) : values_(values) {}

    template <typename T>
    T get(const std::string& key, const T& default_val) const {
        return cy::flowgraph::value_or<T>(values_, key, default_val);
    }

private:
    const cy::flowgraph::ValueMap& values_;
};

template <typename T>
class ArrayView {
public:
    using value_type = T;
    using pointer = T*;
    using reference = T&;

    ArrayView() = default;
    ArrayView(T* data, std::size_t size) : data_(data), size_(size) {}

    T* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    T* begin() const noexcept { return data_; }
    T* end() const noexcept { return data_ + size_; }

    T& operator[](std::size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("ArrayView index out of range");
        }
        return data_[index];
    }

private:
    T* data_ = nullptr;
    std::size_t size_ = 0;
};

template <typename T>
class MatrixView {
public:
    using value_type = T;

    MatrixView() = default;
    MatrixView(T* data, std::size_t rows, std::size_t cols, std::size_t stride)
        : data_(data), rows_(rows), cols_(cols), stride_(stride) {
        if (cols_ > stride_) {
            throw std::invalid_argument("MatrixView cols must not exceed stride");
        }
    }

    T* data() const noexcept { return data_; }
    std::size_t rows() const noexcept { return rows_; }
    std::size_t cols() const noexcept { return cols_; }
    std::size_t stride() const noexcept { return stride_; }
    std::size_t size() const noexcept { return rows_ * cols_; }
    bool empty() const noexcept { return rows_ == 0 || cols_ == 0; }

    ArrayView<T> row(std::size_t row_index) const {
        if (row_index >= rows_) {
            throw std::out_of_range("MatrixView row index out of range");
        }
        return ArrayView<T>(data_ + row_index * stride_, cols_);
    }

    T& operator()(std::size_t row_index, std::size_t col_index) const {
        if (row_index >= rows_ || col_index >= cols_) {
            throw std::out_of_range("MatrixView index out of range");
        }
        return data_[row_index * stride_ + col_index];
    }

private:
    T* data_ = nullptr;
    std::size_t rows_ = 0;
    std::size_t cols_ = 0;
    std::size_t stride_ = 0;
};

template <typename T>
class CubeView {
public:
    using value_type = T;

    CubeView() = default;
    CubeView(T* data, std::size_t channels, std::size_t pulses, std::size_t samples_per_pulse)
        : data_(data),
          channels_(channels),
          pulses_(pulses),
          samples_per_pulse_(samples_per_pulse) {}

    T* data() const noexcept { return data_; }
    std::size_t channels() const noexcept { return channels_; }
    std::size_t pulses() const noexcept { return pulses_; }
    std::size_t samples_per_pulse() const noexcept { return samples_per_pulse_; }
    std::size_t size() const noexcept { return channels_ * pulses_ * samples_per_pulse_; }
    bool empty() const noexcept {
        return channels_ == 0 || pulses_ == 0 || samples_per_pulse_ == 0;
    }

    std::size_t offset(std::size_t channel, std::size_t pulse, std::size_t sample) const {
        if (channel >= channels_ || pulse >= pulses_ || sample >= samples_per_pulse_) {
            throw std::out_of_range("CubeView index out of range");
        }
        return ((pulse * samples_per_pulse_ + sample) * channels_) + channel;
    }

    T& operator()(std::size_t channel, std::size_t pulse, std::size_t sample) const {
        return data_[offset(channel, pulse, sample)];
    }

private:
    T* data_ = nullptr;
    std::size_t channels_ = 0;
    std::size_t pulses_ = 0;
    std::size_t samples_per_pulse_ = 0;
};

struct alignas(8) RawArrayHeader {
    std::uint32_t elem_count;
    std::uint32_t elem_size;
};

static_assert(sizeof(RawArrayHeader) == 8, "RawArrayHeader must stay compact");
static_assert(alignof(RawArrayHeader) == 8, "RawArrayHeader alignment is part of the in-memory contract");

namespace detail {

inline bool is_aligned(const void* ptr, std::size_t alignment) noexcept {
    return alignment == 0 ||
           (reinterpret_cast<std::uintptr_t>(ptr) % alignment) == 0;
}

inline std::size_t align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

template <typename T>
struct RawArrayElementChecks {
    static_assert(std::is_standard_layout<T>::value,
                  "RawBytes array element must be standard-layout");
    static_assert(std::is_trivially_copyable<T>::value,
                  "RawBytes array element must be trivially copyable");
    static_assert(alignof(T) <= alignof(RawArrayHeader),
                  "RawBytes array element alignment greater than 8 is not supported");
};

template <typename T>
std::size_t raw_array_payload_bytes(std::size_t count) {
    (void)sizeof(RawArrayElementChecks<T>);
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("RawBytes array element count exceeds uint32");
    }
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        throw std::overflow_error("RawBytes array payload size overflow");
    }
    return count * sizeof(T);
}

template <typename T>
std::size_t raw_array_frame_bytes(std::size_t count) {
    const std::size_t payload = raw_array_payload_bytes<T>(count);
    if (payload > std::numeric_limits<std::size_t>::max() - sizeof(RawArrayHeader)) {
        throw std::overflow_error("RawBytes array frame size overflow");
    }
    return align_up(sizeof(RawArrayHeader) + payload, alignof(RawArrayHeader));
}

template <typename T>
std::optional<ArrayView<const T>> parse_raw_array_frame(ArrayView<const std::byte> bytes,
                                                        std::size_t* frame_bytes) {
    (void)sizeof(RawArrayElementChecks<T>);
    if (bytes.size() < sizeof(RawArrayHeader)) {
        return std::nullopt;
    }
    if (!is_aligned(bytes.data(), alignof(RawArrayHeader))) {
        return std::nullopt;
    }

    const auto* header = reinterpret_cast<const RawArrayHeader*>(bytes.data());
    if (header->elem_size != sizeof(T)) {
        return std::nullopt;
    }

    const std::size_t total = raw_array_frame_bytes<T>(header->elem_count);
    if (bytes.size() < total) {
        return std::nullopt;
    }

    const auto* payload_ptr = bytes.data() + sizeof(RawArrayHeader);
    if (!is_aligned(payload_ptr, alignof(T))) {
        return std::nullopt;
    }

    if (frame_bytes) {
        *frame_bytes = total;
    }
    return ArrayView<const T>(reinterpret_cast<const T*>(payload_ptr),
                              header->elem_count);
}

} // namespace detail

template <typename T>
std::size_t RawArrayFrameBytes(std::size_t count) {
    return detail::raw_array_frame_bytes<T>(count);
}

template <typename T>
std::optional<ArrayView<const T>> read_raw_array(ArrayView<const std::byte> bytes) {
    return detail::parse_raw_array_frame<T>(bytes, nullptr);
}

template <typename T>
class Reader {
public:
    explicit Reader(cy::flowgraph::PortIn<T>& port) : port_(port) {}

    std::optional<ArrayView<const T>> read(std::size_t count) {
        ensure_read_once();
        if (count == 0 || port_.available() < count) {
            return std::nullopt;
        }

        span_ = port_.get(count);
        if (span_.size() != count) {
            span_ = cy::flowgraph::InputSpan<T>();
            consumed_count_ = 0;
            throw std::runtime_error(
                "Reader::read requires a contiguous window; use read_available or configure buffer capacity as a frame multiple");
        }

        consumed_count_ = count;
        return ArrayView<const T>(span_.data(), span_.size());
    }

    std::optional<ArrayView<const T>> read_available(std::size_t max_count) {
        ensure_read_once();
        const std::size_t count = std::min(port_.available(), max_count);
        if (count == 0) {
            return std::nullopt;
        }

        span_ = port_.get(count);
        if (span_.empty()) {
            return std::nullopt;
        }

        consumed_count_ = span_.size();
        return ArrayView<const T>(span_.data(), span_.size());
    }

    std::optional<MatrixView<const T>> read_matrix(std::size_t rows, std::size_t cols) {
        const std::size_t count = checked_matrix_size(rows, cols);
        auto view = read(count);
        if (!view) {
            return std::nullopt;
        }
        return MatrixView<const T>(view->data(), rows, cols, cols);
    }

    std::optional<CubeView<const T>> read_cube(std::size_t channels,
                                               std::size_t pulses,
                                               std::size_t samples_per_pulse) {
        const std::size_t count = checked_cube_size(channels, pulses, samples_per_pulse);
        auto view = read(count);
        if (!view) {
            return std::nullopt;
        }
        return CubeView<const T>(view->data(), channels, pulses, samples_per_pulse);
    }

    void consume() {
        if (!consumed_ && consumed_count_ > 0) {
            span_.consume(consumed_count_);
            consumed_ = true;
        }
    }

    std::size_t consumed_count() const noexcept { return consumed_count_; }

private:
    void ensure_read_once() {
        if (read_called_) {
            throw std::runtime_error("Reader::read can only be called once per work()");
        }
        read_called_ = true;
    }

    static std::size_t checked_matrix_size(std::size_t rows, std::size_t cols) {
        if (rows != 0 && cols > std::numeric_limits<std::size_t>::max() / rows) {
            throw std::overflow_error("MatrixView size overflow");
        }
        return rows * cols;
    }

    static std::size_t checked_cube_size(std::size_t channels,
                                         std::size_t pulses,
                                         std::size_t samples_per_pulse) {
        if (channels != 0 && pulses > std::numeric_limits<std::size_t>::max() / channels) {
            throw std::overflow_error("CubeView size overflow");
        }
        const std::size_t channel_pulses = channels * pulses;
        if (channel_pulses != 0 &&
            samples_per_pulse > std::numeric_limits<std::size_t>::max() / channel_pulses) {
            throw std::overflow_error("CubeView size overflow");
        }
        return channel_pulses * samples_per_pulse;
    }

    cy::flowgraph::PortIn<T>& port_;
    cy::flowgraph::InputSpan<T> span_;
    std::size_t consumed_count_ = 0;
    bool read_called_ = false;
    bool consumed_ = false;
};

template <>
class Reader<std::byte> {
public:
    explicit Reader(cy::flowgraph::PortIn<std::byte>& port) : port_(port) {}

    std::optional<ArrayView<const std::byte>> read(std::size_t count) {
        ensure_read_once();
        if (count == 0 || port_.available() < count) {
            return std::nullopt;
        }

        span_ = port_.get(count);
        if (span_.size() != count) {
            span_ = cy::flowgraph::InputSpan<std::byte>();
            consumed_count_ = 0;
            throw std::runtime_error(
                "Reader::read requires a contiguous byte window; use read_available or align RawBytes frames to the buffer");
        }

        consumed_count_ = count;
        return ArrayView<const std::byte>(span_.data(), span_.size());
    }

    std::optional<ArrayView<const std::byte>> read_available(std::size_t max_count) {
        ensure_read_once();
        const std::size_t count = std::min(port_.available(), max_count);
        if (count == 0) {
            return std::nullopt;
        }

        span_ = port_.get(count);
        if (span_.empty()) {
            return std::nullopt;
        }

        consumed_count_ = span_.size();
        return ArrayView<const std::byte>(span_.data(), span_.size());
    }

    template <typename Element>
    std::optional<ArrayView<const Element>> read_raw_array() {
        ensure_read_once();
        const std::size_t available = port_.available();
        if (available < sizeof(RawArrayHeader)) {
            return std::nullopt;
        }

        span_ = port_.get(available);
        if (span_.empty()) {
            return std::nullopt;
        }
        if (span_.size() < available && span_.size() < sizeof(RawArrayHeader)) {
            span_ = cy::flowgraph::InputSpan<std::byte>();
            throw std::runtime_error("RawBytes frame header is split across the ring buffer boundary");
        }

        std::size_t frame_bytes = 0;
        auto parsed = detail::parse_raw_array_frame<Element>(
            ArrayView<const std::byte>(span_.data(), span_.size()),
            &frame_bytes);
        if (!parsed) {
            const bool split_frame = span_.size() < available;
            span_ = cy::flowgraph::InputSpan<std::byte>();
            consumed_count_ = 0;
            if (split_frame) {
                throw std::runtime_error("RawBytes frame is split across the ring buffer boundary");
            }
            return std::nullopt;
        }

        consumed_count_ = frame_bytes;
        return parsed;
    }

    void consume() {
        if (!consumed_ && consumed_count_ > 0) {
            span_.consume(consumed_count_);
            consumed_ = true;
        }
    }

    std::size_t consumed_count() const noexcept { return consumed_count_; }

private:
    void ensure_read_once() {
        if (read_called_) {
            throw std::runtime_error("Reader::read can only be called once per work()");
        }
        read_called_ = true;
    }

    cy::flowgraph::PortIn<std::byte>& port_;
    cy::flowgraph::InputSpan<std::byte> span_;
    std::size_t consumed_count_ = 0;
    bool read_called_ = false;
    bool consumed_ = false;
};

template <typename T>
class Writer {
public:
    explicit Writer(cy::flowgraph::PortOut<T>& port) : port_(port) {}

    std::optional<ArrayView<T>> reserve(std::size_t count) {
        ensure_write_once();
        if (count == 0 || port_.available() < count) {
            return std::nullopt;
        }

        span_ = port_.reserve(count);
        if (span_.size() != count) {
            span_ = cy::flowgraph::OutputSpan<T>();
            produced_count_ = 0;
            throw std::runtime_error(
                "Writer::reserve requires contiguous output; use reserve_available or configure buffer capacity as a frame multiple");
        }

        produced_count_ = count;
        return ArrayView<T>(span_.data(), span_.size());
    }

    std::optional<ArrayView<T>> reserve_available(std::size_t max_count) {
        ensure_write_once();
        const std::size_t count = std::min(port_.available(), max_count);
        if (count == 0) {
            return std::nullopt;
        }

        span_ = port_.reserve(count);
        if (span_.empty()) {
            return std::nullopt;
        }

        produced_count_ = span_.size();
        return ArrayView<T>(span_.data(), span_.size());
    }

    std::optional<MatrixView<T>> reserve_matrix(std::size_t rows, std::size_t cols) {
        const std::size_t count = checked_matrix_size(rows, cols);
        auto view = reserve(count);
        if (!view) {
            return std::nullopt;
        }
        return MatrixView<T>(view->data(), rows, cols, cols);
    }

    std::optional<CubeView<T>> reserve_cube(std::size_t channels,
                                            std::size_t pulses,
                                            std::size_t samples_per_pulse) {
        const std::size_t count = checked_cube_size(channels, pulses, samples_per_pulse);
        auto view = reserve(count);
        if (!view) {
            return std::nullopt;
        }
        return CubeView<T>(view->data(), channels, pulses, samples_per_pulse);
    }

    void commit() {
        if (!committed_ && produced_count_ > 0) {
            span_.commit(produced_count_);
            committed_ = true;
        }
    }

    std::size_t produced_count() const noexcept { return produced_count_; }

private:
    void ensure_write_once() {
        if (write_called_) {
            throw std::runtime_error("Writer::reserve can only be called once per work()");
        }
        write_called_ = true;
    }

    static std::size_t checked_matrix_size(std::size_t rows, std::size_t cols) {
        if (rows != 0 && cols > std::numeric_limits<std::size_t>::max() / rows) {
            throw std::overflow_error("MatrixView size overflow");
        }
        return rows * cols;
    }

    static std::size_t checked_cube_size(std::size_t channels,
                                         std::size_t pulses,
                                         std::size_t samples_per_pulse) {
        if (channels != 0 && pulses > std::numeric_limits<std::size_t>::max() / channels) {
            throw std::overflow_error("CubeView size overflow");
        }
        const std::size_t channel_pulses = channels * pulses;
        if (channel_pulses != 0 &&
            samples_per_pulse > std::numeric_limits<std::size_t>::max() / channel_pulses) {
            throw std::overflow_error("CubeView size overflow");
        }
        return channel_pulses * samples_per_pulse;
    }

    cy::flowgraph::PortOut<T>& port_;
    cy::flowgraph::OutputSpan<T> span_;
    std::size_t produced_count_ = 0;
    bool write_called_ = false;
    bool committed_ = false;
};

template <>
class Writer<std::byte> {
public:
    explicit Writer(cy::flowgraph::PortOut<std::byte>& port) : port_(port) {}

    std::optional<ArrayView<std::byte>> reserve(std::size_t count) {
        ensure_write_once();
        if (count == 0 || port_.available() < count) {
            return std::nullopt;
        }

        span_ = port_.reserve(count);
        if (span_.size() != count) {
            span_ = cy::flowgraph::OutputSpan<std::byte>();
            produced_count_ = 0;
            throw std::runtime_error(
                "Writer::reserve requires contiguous byte output; use reserve_available or align RawBytes frames to the buffer");
        }

        produced_count_ = count;
        return ArrayView<std::byte>(span_.data(), span_.size());
    }

    std::optional<ArrayView<std::byte>> reserve_available(std::size_t max_count) {
        ensure_write_once();
        const std::size_t count = std::min(port_.available(), max_count);
        if (count == 0) {
            return std::nullopt;
        }

        span_ = port_.reserve(count);
        if (span_.empty()) {
            return std::nullopt;
        }

        produced_count_ = span_.size();
        return ArrayView<std::byte>(span_.data(), span_.size());
    }

    template <typename Element>
    std::optional<ArrayView<Element>> reserve_raw_array(std::size_t count) {
        (void)sizeof(detail::RawArrayElementChecks<Element>);
        ensure_write_once();

        const std::size_t frame_bytes = detail::raw_array_frame_bytes<Element>(count);
        if (frame_bytes == 0 || port_.available() < frame_bytes) {
            return std::nullopt;
        }

        span_ = port_.reserve(frame_bytes);
        if (span_.size() != frame_bytes) {
            span_ = cy::flowgraph::OutputSpan<std::byte>();
            produced_count_ = 0;
            throw std::runtime_error(
                "Writer::reserve_raw_array requires a contiguous RawBytes frame; configure buffer capacity as a frame multiple");
        }
        if (!detail::is_aligned(span_.data(), alignof(RawArrayHeader))) {
            throw std::runtime_error("RawBytes frame header is not aligned");
        }

        auto* header = reinterpret_cast<RawArrayHeader*>(span_.data());
        header->elem_count = static_cast<std::uint32_t>(count);
        header->elem_size = static_cast<std::uint32_t>(sizeof(Element));

        auto* payload = span_.data() + sizeof(RawArrayHeader);
        if (!detail::is_aligned(payload, alignof(Element))) {
            throw std::runtime_error("RawBytes frame payload is not aligned");
        }

        const std::size_t payload_bytes = detail::raw_array_payload_bytes<Element>(count);
        for (std::size_t i = sizeof(RawArrayHeader) + payload_bytes; i < frame_bytes; ++i) {
            span_[i] = std::byte{0};
        }

        produced_count_ = frame_bytes;
        return ArrayView<Element>(reinterpret_cast<Element*>(payload), count);
    }

    void commit() {
        if (!committed_ && produced_count_ > 0) {
            span_.commit(produced_count_);
            committed_ = true;
        }
    }

    std::size_t produced_count() const noexcept { return produced_count_; }

private:
    void ensure_write_once() {
        if (write_called_) {
            throw std::runtime_error("Writer::reserve can only be called once per work()");
        }
        write_called_ = true;
    }

    cy::flowgraph::PortOut<std::byte>& port_;
    cy::flowgraph::OutputSpan<std::byte> span_;
    std::size_t produced_count_ = 0;
    bool write_called_ = false;
    bool committed_ = false;
};

template <typename Algorithm, typename Tin, typename Tout>
class AlgorithmBlockAdapter : public cy::flowgraph::Block<AlgorithmBlockAdapter<Algorithm, Tin, Tout>> {
public:
    cy::flowgraph::PortIn<Tin> in;
    cy::flowgraph::PortOut<Tout> out;
    CY_MAKE_REFLECTABLE(AlgorithmBlockAdapter, in, out);

    explicit AlgorithmBlockAdapter(const cy::flowgraph::ValueMap& params)
        : algorithm_(std::make_unique<Algorithm>(Params(params))) {}

    void process_work() {
        Reader<Tin> sdk_reader(in);
        Writer<Tout> sdk_writer(out);
        const bool ok = algorithm_->work(sdk_reader, sdk_writer);
        if (!ok) {
            return;
        }
        sdk_writer.commit();
        sdk_reader.consume();
    }

private:
    std::unique_ptr<Algorithm> algorithm_;
};

} // namespace cycore::sdk

namespace cycore::sdk::detail {
template <typename TPlugin, typename TBlock, typename TAlg, typename TIn, typename TOut>
struct AlgorithmRegistrar {
    static void Register(cy::flowgraph::BlockRegistry& registry, const std::string& key) {
        registry.register_block<::cycore::sdk::AlgorithmBlockAdapter<TAlg, TIn, TOut>>(key);
    }
};
} // namespace cycore::sdk::detail

#define CYCORE_EXPORT_ALGORITHM(plugin_name, block_type_name, alg_class, type_in, type_out) \
    CY_PLUGIN( \
        plugin_name, "1.0.0", "Cycore SDK Plugin", "cycore", \
        ::cycore::sdk::detail::AlgorithmRegistrar<void, void, alg_class, type_in, type_out>::Register(plugin.block_registry(), block_type_name); \
    )
