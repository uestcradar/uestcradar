#pragma once

#include <cstddef>

namespace cy::common {

template <typename T>
class Span {
public:
    constexpr Span() noexcept : data_(nullptr), size_(0) {}
    constexpr Span(T* data, std::size_t size) noexcept : data_(data), size_(size) {}

    constexpr T* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr T& operator[](std::size_t idx) const { return data_[idx]; }
    constexpr T* begin() const noexcept { return data_; }
    constexpr T* end() const noexcept { return data_ + size_; }

private:
    T* data_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace cy::common
