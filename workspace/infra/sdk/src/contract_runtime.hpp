#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace uestcradar::internal {

template <class FrameType>
struct ContractTraits;

inline void require_metadata_span(
    std::span<const std::byte> payload,
    std::size_t metadata_bytes,
    const char* name) {
    if (payload.size() < metadata_bytes) {
        throw std::invalid_argument(
            std::string{name} + " metadata is truncated");
    }
}

inline void require_metadata_span(
    std::span<std::byte> payload,
    std::size_t metadata_bytes,
    const char* name) {
    require_metadata_span(
        std::span<const std::byte>{payload.data(), payload.size()},
        metadata_bytes,
        name);
}

inline std::size_t checked_matrix_bytes(
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

template <class T>
T byteswap_if_needed(T value) noexcept {
    static_assert(std::is_integral_v<T>);
    if constexpr (std::endian::native == std::endian::little) {
        return value;
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(__builtin_bswap16(static_cast<std::uint16_t>(value)));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(__builtin_bswap32(static_cast<std::uint32_t>(value)));
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(__builtin_bswap64(static_cast<std::uint64_t>(value)));
    }
}

template <class T>
T load_wire(std::span<const std::byte> payload, std::size_t offset) {
    static_assert(std::is_arithmetic_v<T>);
    if (offset > payload.size() || sizeof(T) > payload.size() - offset) {
        throw std::invalid_argument("wire field exceeds payload boundary");
    }
    if constexpr (std::is_floating_point_v<T>) {
        using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        Bits bits{};
        std::memcpy(&bits, payload.data() + offset, sizeof(bits));
        bits = byteswap_if_needed(bits);
        return std::bit_cast<T>(bits);
    } else {
        T value{};
        std::memcpy(&value, payload.data() + offset, sizeof(value));
        return byteswap_if_needed(value);
    }
}

template <class T>
void store_wire(std::span<std::byte> payload, std::size_t offset, T value) {
    static_assert(std::is_arithmetic_v<T>);
    if (offset > payload.size() || sizeof(T) > payload.size() - offset) {
        throw std::invalid_argument("wire field exceeds payload boundary");
    }
    if constexpr (std::is_floating_point_v<T>) {
        using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        Bits bits = byteswap_if_needed(std::bit_cast<Bits>(value));
        std::memcpy(payload.data() + offset, &bits, sizeof(bits));
    } else {
        value = byteswap_if_needed(value);
        std::memcpy(payload.data() + offset, &value, sizeof(value));
    }
}

}  // namespace uestcradar::internal
