#ifndef CYCORE_BUFFER_H
#define CYCORE_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cy::flowgraph {

class BufferError : public std::runtime_error {
public:
    explicit BufferError(const char* message) : std::runtime_error(message) {}
};

namespace detail {

inline bool is_power_of_two(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

inline std::size_t next_power_of_two(std::size_t value) {
    if (value == 0) {
        return 1;
    }
    --value;
    for (std::size_t shift = 1; shift < sizeof(std::size_t) * 8; shift <<= 1) {
        value |= value >> shift;
    }
    return value + 1;
}

} // namespace detail

} // namespace cy::flowgraph

#endif // CYCORE_BUFFER_H
