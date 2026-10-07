#pragma once
#include "iq_decode.hpp"
#include "dma_snapshot.hpp"
#include "xdma_rx.h"
#include "timestamp_check.hpp"
#include <filesystem>

namespace pcie_source {
struct ReceiveStats {
    std::uint64_t descriptors{}, iq_packets{}, iq_bytes{}, control_packets{}, bit_packets{};
    std::uint64_t invalid_packets{}, changed_copies{};
    TimestampCheck timestamp_check;
    std::array<std::uint64_t, 8> channel_samples{};
};
struct ReceivedBlock {
    std::vector<uestcradar::ComplexInt16> samples;
    bool gap{};
    std::optional<TimestampError> timestamp_error{};
};

class PcieReceiver {
public:
    explicit PcieReceiver(const std::filesystem::path& config_dir);
    ~PcieReceiver();
    PcieReceiver(const PcieReceiver&) = delete;
    PcieReceiver& operator=(const PcieReceiver&) = delete;
    ReceivedBlock poll(unsigned channel);
    const ReceiveStats& stats() const { return stats_; }
    bool synchronized() const { return rx_.synchronized != 0; }
private:
    PcieRx rx_{};
    ReceiveStats stats_{};
    std::vector<std::byte> snapshot_, second_snapshot_;
    DmaSnapshot dma_copy_;
};
} // namespace pcie_source
