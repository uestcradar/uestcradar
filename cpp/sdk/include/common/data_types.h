#ifndef CYCORE_DU_COMMON_DATA_TYPES_H
#define CYCORE_DU_COMMON_DATA_TYPES_H

#include <cstdint>
#include <type_traits>

namespace cy::common {

struct CS16 {
    std::int16_t i;
    std::int16_t q;
};

struct CF32 {
    float i;
    float q;
};

constexpr bool operator==(const CS16& lhs, const CS16& rhs) noexcept {
    return lhs.i == rhs.i && lhs.q == rhs.q;
}

constexpr bool operator!=(const CS16& lhs, const CS16& rhs) noexcept {
    return !(lhs == rhs);
}

constexpr bool operator==(const CF32& lhs, const CF32& rhs) noexcept {
    return lhs.i == rhs.i && lhs.q == rhs.q;
}

constexpr bool operator!=(const CF32& lhs, const CF32& rhs) noexcept {
    return !(lhs == rhs);
}

static_assert(sizeof(CS16) == 4, "CS16 must be exactly 4 bytes");
static_assert(sizeof(CF32) == 8, "CF32 must be exactly 8 bytes");
static_assert(std::is_standard_layout<CS16>::value, "CS16 must be standard-layout");
static_assert(std::is_standard_layout<CF32>::value, "CF32 must be standard-layout");
static_assert(std::is_trivially_copyable<CS16>::value, "CS16 must be trivially copyable");
static_assert(std::is_trivially_copyable<CF32>::value, "CF32 must be trivially copyable");

} // namespace cy::common

#endif // CYCORE_DU_COMMON_DATA_TYPES_H
