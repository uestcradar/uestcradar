#ifndef CYCORE_CIRCULAR_BUFFER_H
#define CYCORE_CIRCULAR_BUFFER_H

#include "buffer.h"
#include "span.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace cy::flowgraph {

namespace detail {

template <typename T, std::size_t Alignment>
class AlignedAllocator {
    static_assert(Alignment != 0 && (Alignment & (Alignment - 1)) == 0,
                  "AlignedAllocator alignment must be a power of two");
    static_assert(Alignment >= alignof(T), "AlignedAllocator alignment must satisfy the value type alignment");

public:
    using value_type = T;

    AlignedAllocator() noexcept = default;

    template <typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    T* allocate(std::size_t n) {
        if (n == 0) {
            return nullptr;
        }
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_array_new_length();
        }
        return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t(Alignment)));
    }

    void deallocate(T* p, std::size_t) noexcept {
        ::operator delete(p, std::align_val_t(Alignment));
    }

    template <typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };
};

template <typename T, typename U, std::size_t Alignment>
bool operator==(const AlignedAllocator<T, Alignment>&, const AlignedAllocator<U, Alignment>&) noexcept {
    return true;
}

template <typename T, typename U, std::size_t Alignment>
bool operator!=(const AlignedAllocator<T, Alignment>&, const AlignedAllocator<U, Alignment>&) noexcept {
    return false;
}

} // namespace detail

template <typename T>
class CircularBuffer {
    static_assert(std::is_trivially_copyable<T>::value,
                  "CircularBuffer<T> Stage 2 supports only trivially copyable sample types");
    static constexpr std::size_t storage_alignment = alignof(T) > 64 ? alignof(T) : 64;
    using Storage = std::vector<T, detail::AlignedAllocator<T, storage_alignment>>;

public:
    class Reader;
    class Writer;

    explicit CircularBuffer(std::size_t requested_capacity)
        : state_(std::make_shared<State>(detail::next_power_of_two(requested_capacity))) {}

    std::size_t size() const noexcept { return state_->capacity; }

    Reader new_reader() {
        auto reader_state = std::make_shared<ReaderState>();
        state_->add_reader(reader_state);
        return Reader(state_, reader_state);
    }

    Writer new_writer() {
        bool expected = false;
        if (!state_->writer_created.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            throw BufferError("CircularBuffer supports only one writer");
        }
        return Writer(state_);
    }

private:
    struct alignas(64) ReaderState {
        std::atomic<std::uint64_t> cursor{0};
        std::atomic<bool> pending{false};
        std::atomic<bool> detached{false};
    };

    struct State {
        using ReaderList = std::vector<std::shared_ptr<ReaderState>>;

        explicit State(std::size_t capacity_value)
            : storage(capacity_value),
              capacity(capacity_value),
              mask(capacity_value - 1),
              readers_snapshot(std::make_shared<ReaderList>()) {
            if (!detail::is_power_of_two(capacity)) {
                throw BufferError("CircularBuffer capacity must be a power of two");
            }
        }

        void add_reader(const std::shared_ptr<ReaderState>& reader) {
            std::lock_guard<std::mutex> lock(readers_mutex);
            reader->cursor.store(write_cursor.load(std::memory_order_acquire),
                                 std::memory_order_relaxed);
            reader->pending.store(false, std::memory_order_relaxed);
            reader->detached.store(false, std::memory_order_relaxed);
            auto current = std::atomic_load_explicit(&readers_snapshot, std::memory_order_acquire);
            auto next = std::make_shared<ReaderList>(current ? *current : ReaderList{});
            next->push_back(reader);
            std::shared_ptr<const ReaderList> published = next;
            std::atomic_store_explicit(&readers_snapshot, published, std::memory_order_release);
            reader->cursor.store(write_cursor.load(std::memory_order_acquire),
                                 std::memory_order_release);
        }

        void detach_reader(const std::shared_ptr<ReaderState>& reader) noexcept {
            if (!reader) {
                return;
            }

            reader->detached.store(true, std::memory_order_release);
            if (!reader->pending.load(std::memory_order_acquire)) {
                try_remove_reader(reader.get());
            }
        }

        void release_reader_span(ReaderState* reader) noexcept {
            reader->pending.store(false, std::memory_order_release);
            if (reader->detached.load(std::memory_order_acquire)) {
                try_remove_reader(reader);
            }
        }

        std::shared_ptr<const ReaderList> readers() const {
            return std::atomic_load_explicit(&readers_snapshot, std::memory_order_acquire);
        }

        std::size_t n_readers() const {
            auto snapshot = readers();
            return snapshot ? snapshot->size() : 0;
        }

        std::uint64_t min_read_cursor(std::uint64_t current_write_cursor) const {
            auto snapshot = readers();
            if (!snapshot || snapshot->empty()) {
                return current_write_cursor;
            }

            std::uint64_t minimum = std::numeric_limits<std::uint64_t>::max();
            for (const auto& reader : *snapshot) {
                minimum = std::min(minimum, reader->cursor.load(std::memory_order_acquire));
            }
            return minimum == std::numeric_limits<std::uint64_t>::max() ? current_write_cursor : minimum;
        }

        bool writer_idle() const noexcept {
            return !writer_pending.load(std::memory_order_acquire);
        }

        static void consume_reader_span(void* state_ptr,
                                        void* reader_ptr,
                                        std::uint64_t read_cursor,
                                        std::size_t n) noexcept {
            auto* state = static_cast<State*>(state_ptr);
            auto* reader = static_cast<ReaderState*>(reader_ptr);
            reader->cursor.store(read_cursor + static_cast<std::uint64_t>(n),
                                 std::memory_order_release);
            state->release_reader_span(reader);
        }

        static void rollback_reader_span(void* state_ptr, void* reader_ptr) noexcept {
            auto* state = static_cast<State*>(state_ptr);
            auto* reader = static_cast<ReaderState*>(reader_ptr);
            state->release_reader_span(reader);
        }

        static void publish_writer_span(void* state_ptr,
                                        std::uint64_t write_cursor,
                                        std::size_t n) noexcept {
            auto* state = static_cast<State*>(state_ptr);
            state->write_cursor.store(write_cursor + static_cast<std::uint64_t>(n),
                                      std::memory_order_release);
            state->writer_pending.store(false, std::memory_order_release);
        }

        static void rollback_writer_span(void* state_ptr) noexcept {
            auto* state = static_cast<State*>(state_ptr);
            state->writer_pending.store(false, std::memory_order_release);
        }

        Storage storage;
        std::size_t capacity = 0;
        std::size_t mask = 0;
        std::atomic<std::uint64_t> write_cursor{0};
        std::atomic<bool> writer_created{false};
        std::atomic<bool> writer_pending{false};
        mutable std::mutex readers_mutex;
        std::shared_ptr<const ReaderList> readers_snapshot;

    private:
        void try_remove_reader(ReaderState* reader) noexcept {
            try {
                remove_reader(reader);
            } catch (...) {
                // Destructors and span rollbacks must not throw. A failed control-plane
                // allocation can leave conservative backpressure in place.
            }
        }

        void remove_reader(ReaderState* reader) {
            std::lock_guard<std::mutex> lock(readers_mutex);
            auto current = std::atomic_load_explicit(&readers_snapshot, std::memory_order_acquire);
            if (!current || current->empty()) {
                return;
            }

            const auto remaining = static_cast<std::size_t>(
                std::count_if(current->begin(), current->end(), [reader](const std::shared_ptr<ReaderState>& item) {
                    return item.get() != reader;
                }));
            if (remaining == current->size()) {
                return;
            }

            auto next = std::make_shared<ReaderList>();
            next->reserve(remaining);
            for (const auto& item : *current) {
                if (item.get() != reader) {
                    next->push_back(item);
                }
            }

            std::shared_ptr<const ReaderList> published = next;
            std::atomic_store_explicit(&readers_snapshot, published, std::memory_order_release);
        }
    };

public:
    class Reader {
    public:
        Reader() = default;
        Reader(const Reader&) = delete;
        Reader& operator=(const Reader&) = delete;

        Reader(Reader&& other) noexcept
            : state_(std::move(other.state_)), reader_(std::move(other.reader_)) {}

        Reader& operator=(Reader&& other) noexcept {
            if (this != &other) {
                disconnect();
                state_ = std::move(other.state_);
                reader_ = std::move(other.reader_);
            }
            return *this;
        }

        ~Reader() {
            disconnect();
        }

        InputSpan<T> get(std::size_t requested) {
            ensure_valid();
            if (requested == 0) {
                return InputSpan<T>();
            }

            bool expected = false;
            if (!reader_->pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                throw BufferError("CircularBuffer reader already has an outstanding span");
            }

            const std::uint64_t read_cursor = reader_->cursor.load(std::memory_order_acquire);
            const std::uint64_t write_cursor = state_->write_cursor.load(std::memory_order_acquire);
            const std::uint64_t produced = write_cursor - read_cursor;
            const std::size_t readable = static_cast<std::size_t>(std::min<std::uint64_t>(produced, state_->capacity));
            const std::size_t index = static_cast<std::size_t>(read_cursor) & state_->mask;
            const std::size_t contiguous = std::min(readable, state_->capacity - index);
            const std::size_t span_size = std::min(requested, contiguous);
            if (span_size == 0) {
                reader_->pending.store(false, std::memory_order_release);
                return InputSpan<T>();
            }

            std::shared_ptr<void> state_owner = state_;
            std::shared_ptr<void> reader_owner = reader_;
            return InputSpan<T>(
                state_->storage.data() + index,
                span_size,
                std::move(state_owner),
                std::move(reader_owner),
                read_cursor,
                &State::consume_reader_span,
                &State::rollback_reader_span);
        }

        std::size_t available() const {
            ensure_valid();
            const std::uint64_t write_cursor = state_->write_cursor.load(std::memory_order_acquire);
            const std::uint64_t read_cursor = reader_->cursor.load(std::memory_order_acquire);
            const std::uint64_t produced = write_cursor - read_cursor;
            return static_cast<std::size_t>(std::min<std::uint64_t>(produced, state_->capacity));
        }

        std::uint64_t position() const {
            ensure_valid();
            return reader_->cursor.load(std::memory_order_acquire);
        }

        bool connected() const noexcept { return static_cast<bool>(state_) && static_cast<bool>(reader_); }

        void disconnect() noexcept {
            if (state_ && reader_) {
                state_->detach_reader(reader_);
            }
            state_.reset();
            reader_.reset();
        }

    private:
        friend class CircularBuffer<T>;

        Reader(std::shared_ptr<State> state, std::shared_ptr<ReaderState> reader)
            : state_(std::move(state)), reader_(std::move(reader)) {}

        void ensure_valid() const {
            if (!state_ || !reader_) {
                throw BufferError("CircularBuffer reader is not connected");
            }
        }

        std::shared_ptr<State> state_;
        std::shared_ptr<ReaderState> reader_;
    };

    class Writer {
    public:
        Writer() = default;
        Writer(const Writer&) = delete;
        Writer& operator=(const Writer&) = delete;

        Writer(Writer&& other) noexcept : state_(std::move(other.state_)) {}

        Writer& operator=(Writer&& other) noexcept {
            if (this != &other) {
                state_ = std::move(other.state_);
            }
            return *this;
        }

        OutputSpan<T> reserve(std::size_t requested) {
            ensure_valid();
            if (requested == 0) {
                return OutputSpan<T>();
            }

            bool expected = false;
            if (!state_->writer_pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                throw BufferError("CircularBuffer writer already has an outstanding span");
            }

            const std::uint64_t write_cursor = state_->write_cursor.load(std::memory_order_acquire);
            const std::uint64_t oldest = state_->min_read_cursor(write_cursor);
            const std::uint64_t used = write_cursor - oldest;
            const std::size_t writable = used >= state_->capacity ? 0 : static_cast<std::size_t>(state_->capacity - used);
            const std::size_t index = static_cast<std::size_t>(write_cursor) & state_->mask;
            const std::size_t contiguous = std::min(writable, state_->capacity - index);
            const std::size_t span_size = std::min(requested, contiguous);
            if (span_size == 0) {
                state_->writer_pending.store(false, std::memory_order_release);
                return OutputSpan<T>();
            }

            std::shared_ptr<void> state_owner = state_;
            return OutputSpan<T>(
                state_->storage.data() + index,
                span_size,
                std::move(state_owner),
                write_cursor,
                &State::publish_writer_span,
                &State::rollback_writer_span);
        }

        OutputSpan<T> tryReserve(std::size_t requested) {
            return reserve(requested);
        }

        std::size_t available() const {
            ensure_valid();
            const std::uint64_t write_cursor = state_->write_cursor.load(std::memory_order_acquire);
            const std::uint64_t oldest = state_->min_read_cursor(write_cursor);
            const std::uint64_t used = write_cursor - oldest;
            if (used >= state_->capacity) {
                return 0;
            }
            return static_cast<std::size_t>(state_->capacity - used);
        }

        std::uint64_t position() const {
            ensure_valid();
            return state_->write_cursor.load(std::memory_order_acquire);
        }

        std::size_t n_readers() const {
            ensure_valid();
            return state_->n_readers();
        }

        bool idle() const {
            ensure_valid();
            return state_->writer_idle();
        }

        bool connected() const noexcept { return static_cast<bool>(state_); }

    private:
        friend class CircularBuffer<T>;

        explicit Writer(std::shared_ptr<State> state) : state_(std::move(state)) {}

        void ensure_valid() const {
            if (!state_) {
                throw BufferError("CircularBuffer writer is not connected");
            }
        }

        std::shared_ptr<State> state_;
    };

    std::shared_ptr<State> state_;
};

} // namespace cy::flowgraph

#endif // CYCORE_CIRCULAR_BUFFER_H
