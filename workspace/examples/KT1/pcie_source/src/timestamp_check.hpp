#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>

namespace pcie_source {
struct ControlTimestamps {
    std::uint64_t tx_start{}, rx_first{};
};

// CTRL_HEAD, TX low/high, RX low/high, CTRL_TAIL. TX may be all F.
inline ControlTimestamps read_control_timestamps(std::span<const std::byte> payload) {
    if (payload.size() < 20 || payload.data() == nullptr)
        throw std::invalid_argument("control payload is too short for RX timestamp");
    const auto le64 = [&](std::size_t offset) {
        std::uint64_t value = 0;
        for (unsigned byte = 0; byte < 8; ++byte)
            value |= std::uint64_t{std::to_integer<unsigned>(payload[offset + byte])} << (8 * byte);
        return value;
    };
    return {le64(4), le64(12)};
}

struct ControlObservation {
    ControlTimestamps timestamps;
    std::uint64_t control_packet{}, descriptor{}, monotonic_ns{};
    std::uint32_t offset{}, bytes{}, mask{};
};
struct TimestampError {
    ControlObservation previous, current;
    std::uint64_t delta;
};

// Passive diagnostics: no IQ acceptance or CPI segmentation decisions here.
struct TimestampCheck {
    static constexpr std::uint64_t expected_delta = 98304;
    std::uint64_t observations{}, comparisons{}, consecutive{}, errors{}, wraps{};
    std::optional<ControlObservation> previous;

    std::optional<TimestampError> observe(const ControlObservation& current) {
        ++observations;
        std::optional<TimestampError> error;
        if (previous) {
            ++comparisons;
            const auto delta = current.timestamps.rx_first - previous->timestamps.rx_first;
            if (delta == expected_delta) {
                ++consecutive;
                if (current.timestamps.rx_first < previous->timestamps.rx_first) ++wraps;
            } else {
                ++errors;
                error = TimestampError{*previous, current, delta};
            }
        }
        previous = current; // Compare adjacent observations, even after an error.
        return error;
    }
};

inline void write_timestamp_error(std::ostream& out, std::uint64_t run_id,
                                 unsigned channel, const TimestampError& error) {
    // Decimal strings preserve uint64 precision in JSON consumers.
    const auto observation = [&](const ControlObservation& value) {
        out << "{\"rx_first\":\"" << value.timestamps.rx_first
            << "\",\"tx_start\":\"" << value.timestamps.tx_start
            << "\",\"control_packet\":\"" << value.control_packet
            << "\",\"descriptor\":\"" << value.descriptor
            << "\",\"monotonic_ns\":\"" << value.monotonic_ns
            << "\",\"offset\":" << value.offset << ",\"bytes\":" << value.bytes
            << ",\"mask\":" << value.mask << '}';
    };
    out << "{\"event\":\"rx_timestamp_error\",\"run_id\":\"" << run_id
        << "\",\"channel\":" << channel << ",\"pipe\":0,\"expected_delta\":" << TimestampCheck::expected_delta
        << ",\"delta_u64\":\"" << error.delta << "\",\"previous\":";
    observation(error.previous);
    out << ",\"current\":";
    observation(error.current);
    out << "}\n";
}
} // namespace pcie_source
