#ifndef CYCORE_SPAN_H
#define CYCORE_SPAN_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace cy::flowgraph {

template <typename T>
class InputSpan {
public:
    using value_type = T;
    using ConsumeFn = void (*)(void*, void*, std::uint64_t, std::size_t) noexcept;
    using RollbackFn = void (*)(void*, void*) noexcept;

    InputSpan() = default;

    InputSpan(const T* data,
              std::size_t size,
              std::shared_ptr<void> owner,
              std::shared_ptr<void> cursor_owner,
              std::uint64_t cursor,
              ConsumeFn consume,
              RollbackFn rollback)
        : data_(data),
          size_(size),
          owner_(std::move(owner)),
          cursor_owner_(std::move(cursor_owner)),
          cursor_(cursor),
          consume_(consume),
          rollback_(rollback),
          consumed_(false) {}

    InputSpan(const InputSpan&) = delete;
    InputSpan& operator=(const InputSpan&) = delete;

    InputSpan(InputSpan&& other) noexcept {
        move_from(std::move(other));
    }

    InputSpan& operator=(InputSpan&& other) noexcept {
        if (this != &other) {
            rollback_if_needed();
            move_from(std::move(other));
        }
        return *this;
    }

    ~InputSpan() {
        rollback_if_needed();
    }

    const T* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    const T& operator[](std::size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("InputSpan index out of range");
        }
        return data_[index];
    }

    void consume(std::size_t n) {
        if (n > size_) {
            throw std::out_of_range("InputSpan consume count exceeds span size");
        }
        if (consumed_) {
            if (n == 0 && size_ == 0) {
                return;
            }
            throw std::logic_error("InputSpan has already been consumed");
        }
        if (consume_) {
            consume_(owner_.get(), cursor_owner_.get(), cursor_, n);
        }
        consumed_ = true;
    }

private:
    void rollback_if_needed() noexcept {
        if (!consumed_ && rollback_) {
            rollback_(owner_.get(), cursor_owner_.get());
        }
        consumed_ = true;
    }

    void move_from(InputSpan&& other) noexcept {
        data_ = other.data_;
        size_ = other.size_;
        owner_ = std::move(other.owner_);
        cursor_owner_ = std::move(other.cursor_owner_);
        cursor_ = other.cursor_;
        consume_ = other.consume_;
        rollback_ = other.rollback_;
        consumed_ = other.consumed_;

        other.data_ = nullptr;
        other.size_ = 0;
        other.cursor_ = 0;
        other.consume_ = nullptr;
        other.rollback_ = nullptr;
        other.consumed_ = true;
    }

    const T* data_ = nullptr;
    std::size_t size_ = 0;
    std::shared_ptr<void> owner_;
    std::shared_ptr<void> cursor_owner_;
    std::uint64_t cursor_ = 0;
    ConsumeFn consume_ = nullptr;
    RollbackFn rollback_ = nullptr;
    bool consumed_ = true;
};

template <typename T>
class OutputSpan {
public:
    using value_type = T;
    using PublishFn = void (*)(void*, std::uint64_t, std::size_t) noexcept;
    using RollbackFn = void (*)(void*) noexcept;
    using CommitProbeFn = void (*)(void*, const T*, std::size_t) noexcept;

    OutputSpan() = default;

    OutputSpan(T* data,
               std::size_t size,
               std::shared_ptr<void> owner,
               std::uint64_t cursor,
               PublishFn publish,
               RollbackFn rollback)
        : data_(data),
          size_(size),
          owner_(std::move(owner)),
          cursor_(cursor),
          publish_(publish),
          rollback_(rollback),
          committed_(false) {}

    OutputSpan(const OutputSpan&) = delete;
    OutputSpan& operator=(const OutputSpan&) = delete;

    OutputSpan(OutputSpan&& other) noexcept {
        move_from(std::move(other));
    }

    OutputSpan& operator=(OutputSpan&& other) noexcept {
        if (this != &other) {
            rollback_if_needed();
            move_from(std::move(other));
        }
        return *this;
    }

    ~OutputSpan() {
        rollback_if_needed();
    }

    T* data() noexcept { return data_; }
    const T* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    T& operator[](std::size_t index) {
        if (index >= size_) {
            throw std::out_of_range("OutputSpan index out of range");
        }
        return data_[index];
    }

    const T& operator[](std::size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("OutputSpan index out of range");
        }
        return data_[index];
    }

    void commit(std::size_t n) {
        if (n > size_) {
            throw std::out_of_range("OutputSpan commit count exceeds span size");
        }
        if (committed_) {
            if (n == 0 && size_ == 0) {
                return;
            }
            throw std::logic_error("OutputSpan has already been committed");
        }
        if (probe_commit_) {
            probe_commit_(probe_.get(), data_, n);
        }
        if (publish_) {
            publish_(owner_.get(), cursor_, n);
        }
        committed_ = true;
    }

    void publish(std::size_t n) {
        commit(n);
    }

    void set_commit_probe(std::shared_ptr<void> probe, CommitProbeFn commit_probe) noexcept {
        probe_ = std::move(probe);
        probe_commit_ = commit_probe;
    }

private:
    void rollback_if_needed() noexcept {
        if (!committed_ && rollback_) {
            rollback_(owner_.get());
        }
        committed_ = true;
    }

    void move_from(OutputSpan&& other) noexcept {
        data_ = other.data_;
        size_ = other.size_;
        owner_ = std::move(other.owner_);
        cursor_ = other.cursor_;
        publish_ = other.publish_;
        rollback_ = other.rollback_;
        probe_ = std::move(other.probe_);
        probe_commit_ = other.probe_commit_;
        committed_ = other.committed_;

        other.data_ = nullptr;
        other.size_ = 0;
        other.cursor_ = 0;
        other.publish_ = nullptr;
        other.rollback_ = nullptr;
        other.probe_commit_ = nullptr;
        other.committed_ = true;
    }

    T* data_ = nullptr;
    std::size_t size_ = 0;
    std::shared_ptr<void> owner_;
    std::shared_ptr<void> probe_;
    std::uint64_t cursor_ = 0;
    PublishFn publish_ = nullptr;
    RollbackFn rollback_ = nullptr;
    CommitProbeFn probe_commit_ = nullptr;
    bool committed_ = true;
};

} // namespace cy::flowgraph

#endif // CYCORE_SPAN_H
