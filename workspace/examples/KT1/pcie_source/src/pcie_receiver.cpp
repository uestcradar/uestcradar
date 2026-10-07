#include "pcie_receiver.hpp"
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <system_error>

namespace pcie_source {
PcieReceiver::PcieReceiver(const std::filesystem::path& config_dir) {
    static_assert(std::endian::native == std::endian::little,
                  "legacy MMIO/control protocol requires a verified little-endian host");
    PcieConfig sync{}, drp{};
    for (const auto& [name, config] : {
            std::pair{"config_sync_4.txt", &sync}, std::pair{"config_drp_4g8.txt", &drp}}) {
        const auto path = config_dir / name;
        if (pcie_read_config(path.c_str(), config) != 0)
            throw std::system_error(errno, std::generic_category(), "read config " + path.string());
    }
    // All configuration files validated before the first register write.
    if (pcie_rx_open(&rx_, &sync, &drp) != 0)
        throw std::system_error(errno, std::generic_category(), "open/map PCIe receiver");
}

PcieReceiver::~PcieReceiver() {
    dma_copy_.stop(); // Join before unmapping DMA, including shutdown after an error.
    pcie_rx_close(&rx_);
}

ReceivedBlock PcieReceiver::poll(unsigned channel) {
    PcieDescriptor descriptor{};
    const int status = pcie_rx_poll(&rx_, &descriptor);
    if (status == 0) return {};
    ++stats_.descriptors;
    if (status < 0) {
        ++stats_.invalid_packets;
        stats_.timestamp_check.previous.reset();
        return {{}, true};
    }
    if (descriptor.group == 15) { ++stats_.bit_packets; return {}; }
    PacketView packet{};
    packet.mask = descriptor.mask;
    packet.group = descriptor.group;
    packet.control = descriptor.control != 0;
    for (std::size_t p = 0; p < 4; ++p) {
        const auto* address = static_cast<const std::byte*>(rx_.rx) +
            p * PCIE_PIPE_BYTES + descriptor.offset;
        packet.pipes[p] = {address, descriptor.bytes};
    }
    if (packet.control) {
        ++stats_.control_packets;
        if (descriptor.bytes < 20) {
            ++stats_.invalid_packets;
            stats_.timestamp_check.previous.reset();
            return {{}, true};
        }
        std::array<std::byte, 20> prefix{};
        std::memcpy(prefix.data(), packet.pipes[0].data(), prefix.size());
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const ControlObservation observation{read_control_timestamps(prefix),
            stats_.control_packets, stats_.descriptors,
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()),
            descriptor.offset, descriptor.bytes, descriptor.mask};
        // Even anomalous timestamps leave IQ and partial software CPIs untouched.
        return {{}, false, stats_.timestamp_check.observe(observation)};
    }
    if (channel >= 8) throw std::invalid_argument("channel must be 0..7");
    unsigned selected_pipe = 0;
    for (unsigned p = 0; p < pipe_channels.size(); ++p)
        if (pipe_channels[p][0] == channel || pipe_channels[p][1] == channel) selected_pipe = p;
    // Bulk DMA reads, then decode in cacheable host RAM. Reuse the two snapshots.
    snapshot_.resize(descriptor.bytes);
    second_snapshot_.resize(descriptor.bytes);
    const auto* dma = packet.pipes[selected_pipe].data();
    dma_copy_.copy(snapshot_, second_snapshot_, {dma, descriptor.bytes});
    packet.pipes[selected_pipe] = snapshot_;
    auto samples = decode_channel(packet, channel);
    packet.pipes[selected_pipe] = second_snapshot_;
    const auto again = decode_channel(packet, channel);
    // Diagnostic only: equality is NOT a DMA ownership/continuity guarantee.
    if (std::memcmp(samples.data(), again.data(), samples.size() * sizeof(samples[0])) != 0) {
        ++stats_.changed_copies;
        return {{}, true};
    }
    ++stats_.iq_packets;
    stats_.iq_bytes += std::uint64_t{descriptor.bytes} * 4;
    for (auto& count : stats_.channel_samples) count += descriptor.bytes / 8;
    return {std::move(samples), false};
}
} // namespace pcie_source
