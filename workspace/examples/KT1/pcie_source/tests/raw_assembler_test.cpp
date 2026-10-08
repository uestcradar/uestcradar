#include "raw_assembler.hpp"
#include <cassert>

int main() {
    using pcie_source::RawAssembler;
    const auto fails = [](auto run) { try { run(); } catch (const std::runtime_error&) { return true; } return false; };
    std::vector<uestcradar::ComplexInt16> first(4096, {12, -34}), second(4096, {-56, 78});
    RawAssembler assembler;
    assert(!assembler.append(first) && assembler.startup_samples() == 4096);
    assembler.control({UINT64_MAX, UINT64_C(0xfedcba9876543210)});
    assert(!assembler.append(first));
    auto frame = assembler.append(second);
    assert(frame && frame->metadata.channel_count == 1 && frame->metadata.samples_per_channel == 8192);
    assert(frame->metadata.tx_timestamp == UINT64_MAX && frame->metadata.rx_timestamp == UINT64_C(0xfedcba9876543210));
    assert(frame->samples[0].i == 12 && frame->samples[4095].q == -34);
    assert(frame->samples[4096].i == -56 && frame->samples[8191].q == 78);
    assert(assembler.completed() == 1 && assembler.partial_samples() == 0);
    assert(fails([&] { assembler.append(first); })); // No attaching extra samples to old timestamps.
    assembler.control({123, 456});
    auto next = assembler.append(frame->samples); // Combined descriptor is also legal.
    assert(next && next->metadata.tx_timestamp == 123 && next->metadata.rx_timestamp == 456);
    assert(frame->samples[4096].i == -56); // Earlier frame owns its memory.
    RawAssembler short_frame;
    short_frame.control({1, 2});
    assert(!short_frame.append(first) && short_frame.partial_samples() == 4096);
    assert(fails([&] { short_frame.control({3, 4}); }));
    RawAssembler duplicate_control;
    duplicate_control.control({1, 2});
    assert(fails([&] { duplicate_control.control({1, 2}); }));
    RawAssembler oversized;
    oversized.control({1, 2});
    std::vector<uestcradar::ComplexInt16> too_many(8193);
    assert(fails([&] { oversized.append(too_many); }));
}
