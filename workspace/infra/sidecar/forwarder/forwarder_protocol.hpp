#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sidecar::forwarder::protocol {

inline constexpr std::uint64_t kPayloadTag = 0x4657445f44415441ULL;
inline constexpr std::uint64_t kCreditTag = 0x4657445f43524454ULL;
inline constexpr std::uint64_t kHelloTag = 0x4657445f48454c4fULL;
inline constexpr std::uint32_t kProtocolVersion = 3;

enum class PortRole : std::uint32_t {
    producer = 1,
    consumer = 2,
};

using CreditBytes = std::array<std::byte, sizeof(std::uint64_t)>;
using HelloBytes = std::array<std::byte, 24>;

struct PortContract {
    std::uint64_t type_id;
    std::uint32_t type_version;
    std::uint32_t max_payload_bytes;
};

struct Hello {
    PortRole role;
    PortContract contract;
};

template <class Bytes>
inline void encode_unsigned(
    Bytes& bytes,
    std::size_t offset,
    std::uint64_t value,
    std::size_t width) noexcept {
    for (std::size_t index = 0; index < width; ++index) {
        const unsigned shift =
            static_cast<unsigned>((width - index - 1) * 8);
        bytes[offset + index] =
            static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

template <class Bytes>
inline std::uint64_t decode_unsigned(
    const Bytes& bytes,
    std::size_t offset,
    std::size_t width) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value = (value << 8) |
                std::to_integer<unsigned char>(bytes[offset + index]);
    }
    return value;
}

inline CreditBytes encode_credit(std::size_t value) noexcept {
    CreditBytes bytes{};
    encode_unsigned(bytes, 0, value, bytes.size());
    return bytes;
}

inline std::uint64_t decode_credit(
    const CreditBytes& bytes) noexcept {
    return decode_unsigned(bytes, 0, bytes.size());
}

inline HelloBytes encode_hello(const Hello& hello) noexcept {
    HelloBytes bytes{};
    encode_unsigned(bytes, 0, kProtocolVersion, 4);
    encode_unsigned(
        bytes, 4, static_cast<std::uint32_t>(hello.role), 4);
    encode_unsigned(bytes, 8, hello.contract.type_id, 8);
    encode_unsigned(bytes, 16, hello.contract.type_version, 4);
    encode_unsigned(bytes, 20, hello.contract.max_payload_bytes, 4);
    return bytes;
}

inline bool decode_hello(
    const HelloBytes& bytes,
    Hello& hello) noexcept {
    if (decode_unsigned(bytes, 0, 4) != kProtocolVersion) {
        return false;
    }
    const auto role = static_cast<PortRole>(
        decode_unsigned(bytes, 4, 4));
    if (role != PortRole::producer && role != PortRole::consumer) {
        return false;
    }
    hello = {
        role,
        {
            decode_unsigned(bytes, 8, 8),
            static_cast<std::uint32_t>(
                decode_unsigned(bytes, 16, 4)),
            static_cast<std::uint32_t>(
                decode_unsigned(bytes, 20, 4)),
        },
    };
    return hello.contract.type_id != 0 &&
           hello.contract.type_version != 0 &&
           hello.contract.max_payload_bytes != 0;
}

}  // namespace sidecar::forwarder::protocol
