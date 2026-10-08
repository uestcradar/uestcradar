#include "pcie_receiver.hpp"
#include "cpi_assembler.hpp"
#include "raw_assembler.hpp"
#include <cassert>
#include <sys/mman.h>

// Exercise the real PcieReceiver::poll, replacing only hardware access.
namespace {
PcieDescriptor descriptor{};
std::uint64_t next_rx{};
int next_status{};
int iq_marker{};
}
extern "C" {
int pcie_read_config(const char*, PcieConfig*) { return 0; }
int pcie_rx_open(PcieRx* rx, const PcieConfig*, const PcieConfig*) {
    rx->rx = mmap(nullptr, PCIE_RX_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return rx->rx == MAP_FAILED ? -1 : 0; // Virtual mapping; only touched pages consume RAM.
}
void pcie_rx_close(PcieRx* rx) { munmap(rx->rx, PCIE_RX_BYTES); }
int pcie_rx_poll(PcieRx* rx, PcieDescriptor* out) {
    *out = descriptor;
    if (next_status == 1 && descriptor.control) {
        auto* bytes = static_cast<std::byte*>(rx->rx) + descriptor.offset;
        for (unsigned i = 0; i < 8; ++i) {
            bytes[4 + i] = std::byte{255}; // TX absent is allowed.
            bytes[12 + i] = std::byte((next_rx >> (8 * i)) & 255);
        }
    } else if (next_status == 1 && descriptor.group == 0) {
        for (unsigned p = 0; p < 4; ++p)
            for (unsigned t = 0; t < descriptor.bytes / 8; ++t) {
                const auto i = static_cast<std::int16_t>(iq_marker + p * 1000 + t);
                const uestcradar::ComplexInt16 values[2]{{i, static_cast<std::int16_t>(-i)},
                    {static_cast<std::int16_t>(i + 100), static_cast<std::int16_t>(-i - 100)}};
                std::memcpy(static_cast<std::byte*>(rx->rx) + p * PCIE_PIPE_BYTES + descriptor.offset + t * 8,
                            values, sizeof(values));
            }
    }
    return next_status;
}
}

int main() {
    using namespace pcie_source;
    PcieReceiver receiver("unused");
    Templates templates{};
    CpiAssembler assembler(templates);
    assert(receiver.poll(0).samples.empty() && receiver.stats().descriptors == 0);
    const auto feed = [&](bool control, std::uint64_t rx, unsigned bytes = 24) {
        descriptor = {2048, bytes, 15, 0, control};
        next_status = 1;
        next_rx = rx;
        const auto before = receiver.stats().descriptors;
        auto block = receiver.poll(0);
        assert(receiver.stats().descriptors == before + 1); // Control activity isn't idle.
        if (block.gap) assembler.discard_partial();
        assembler.append(block.samples, [](Cpi) {});
        return block;
    };
    const auto first_control = feed(true, 100);
    assert(!first_control.timestamp_error && first_control.control);
    assert(first_control.control->tx_start == UINT64_MAX && first_control.control->rx_first == 100);
    assert(!feed(false, 0, 8).samples.empty());
    auto block = feed(true, 100); // Duplicate timestamp doesn't clear the partial CPI.
    assert(block.timestamp_error && !block.gap && block.samples.empty());
    assert(block.timestamp_error->previous.control_packet == 1);
    assert(block.timestamp_error->current.control_packet == 2);
    assert(block.timestamp_error->previous.descriptor == 1);
    assert(block.timestamp_error->current.descriptor == 3);
    assert(assembler.discard_partial() == 1);
    assert(!feed(true, 98404).timestamp_error);
    block = feed(true, 98404 + 196608);
    assert(block.timestamp_error && block.timestamp_error->delta == 196608 && !block.gap);
    assert(feed(true, 0, 19).gap); // Short payload retains safety rejection.
    assert(!receiver.stats().timestamp_check.previous);
    assert(!feed(true, 500).timestamp_error); // Re-establish baseline after unreadable control.
    assert(!feed(true, 500 + 98304).timestamp_error);
    assert(receiver.stats().timestamp_check.errors == 2);
    assert(receiver.stats().invalid_packets == 1);

    // Bulk-copy blocks plus short tail; returned IQ owns its data across later polls.
    iq_marker = 42;
    const auto owned = feed(false, 0, 33 * 8);
    assert(owned.samples.size() == 33);
    assert(owned.samples[0].i == 42 && owned.samples[32].q == -74);
    iq_marker = 1000;
    for (unsigned channel = 0; channel < 8; ++channel) {
        constexpr unsigned pipes[]{0, 0, 2, 3, 1, 1, 2, 3};
        constexpr unsigned lanes[]{0, 1, 0, 0, 0, 1, 1, 1};
        descriptor.bytes = 32768;
        const auto result = receiver.poll(channel);
        assert(!result.gap && result.samples.size() == 4096);
        for (unsigned t = 0; t < result.samples.size(); ++t) {
            const int expected = iq_marker + pipes[channel] * 1000 + lanes[channel] * 100 + t;
            assert(result.samples[t].i == expected && result.samples[t].q == -expected);
        }
    }
    assert(owned.samples[0].i == 42 && owned.samples[32].q == -74);
    const auto invalid_before = receiver.stats().invalid_packets;
    descriptor = {2048, 8193 * 8, 15, 0, 0};
    assert(receiver.poll(0).gap && receiver.stats().invalid_packets == invalid_before + 1);
    // Exercise real receiver + new frame assembler for each selected channel.
    for (unsigned channel = 0; channel < 8; ++channel) {
        constexpr unsigned pipes[]{0, 0, 2, 3, 1, 1, 2, 3};
        constexpr unsigned lanes[]{0, 1, 0, 0, 0, 1, 1, 1};
        RawAssembler raw;
        descriptor = {2048, 24, 15, 0, 1};
        next_rx = UINT64_C(0xf000000000000000) + channel;
        const auto control = receiver.poll(channel);
        assert(control.control);
        raw.control(*control.control);
        descriptor.control = 0;
        descriptor.bytes = 32768;
        iq_marker = 100;
        assert(!raw.append(receiver.poll(channel).samples));
        iq_marker = 10000;
        const auto frame = raw.append(receiver.poll(channel).samples);
        assert(frame && frame->metadata.tx_timestamp == UINT64_MAX && frame->metadata.rx_timestamp == next_rx);
        for (unsigned t = 0; t < 8192; ++t) {
            const int value = (t < 4096 ? 100 : 10000) + pipes[channel] * 1000 + lanes[channel] * 100 + t % 4096;
            assert(frame->samples[t].i == value && frame->samples[t].q == -value);
        }
    }
}
