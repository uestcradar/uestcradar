#pragma once
#include "raw_iq.hpp"
#include "timestamp_check.hpp"
#include <optional>
#include <stdexcept>

namespace pcie_source {
// RTL control table precedes two 4096-point segments. PCIe descriptors may split
// or combine these segments; their total must be exactly one hardware frame.
class RawAssembler {
public:
    static constexpr std::uint32_t samples_per_frame = 8192;
    void control(ControlTimestamps timestamps) {
        if (collecting_) throw std::runtime_error("control arrived before the previous IQ frame was complete");
        current_ = {{timestamps.tx_start, timestamps.rx_first, 1, samples_per_frame}, {}};
        current_.samples.reserve(samples_per_frame);
        anchored_ = collecting_ = true;
    }
    std::optional<raw_iq::Block> append(std::span<const uestcradar::ComplexInt16> samples) {
        if (samples.empty()) return std::nullopt;
        if (!anchored_) { startup_samples_ += samples.size(); return std::nullopt; }
        if (!collecting_) throw std::runtime_error("IQ arrived without a new control table");
        if (samples.size() > samples_per_frame - current_.samples.size())
            throw std::runtime_error("IQ exceeds its control table frame boundary");
        current_.samples.insert(current_.samples.end(), samples.begin(), samples.end());
        if (current_.samples.size() != samples_per_frame) return std::nullopt;
        collecting_ = false;
        ++completed_;
        return std::move(current_);
    }
    std::size_t partial_samples() const { return collecting_ ? current_.samples.size() : 0; }
    std::uint64_t startup_samples() const { return startup_samples_; }
    std::uint64_t completed() const { return completed_; }
private:
    raw_iq::Block current_{};
    bool anchored_{}, collecting_{};
    std::uint64_t startup_samples_{}, completed_{};
};
} // namespace pcie_source
