#include "ringbuf.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main() {
    const std::string name =
        "/uestcradar_slot_v6_test_" + std::to_string(::getpid());
    RingBuffer* ring = nullptr;
    try {
        ring = ringbuf_create(name.c_str(), {3, 128, 0x42, 7});
        RingWriteLease write;
        require(
            ringbuf_reserve(ring, write) == RingResult::ok,
            "first Slot was not reservable");
        require(
            std::all_of(
                std::begin(write.envelope().reserved),
                std::end(write.envelope().reserved),
                [](std::byte value) { return value == std::byte{}; }),
            "reserved Envelope bytes were not cleared");
        write.envelope() = {
            .frame_id = 1,
            .timestamp = 2,
            .type_id = 0x42,
            .type_version = 7,
            .payload_length = 129,
        };
        require(
            ringbuf_commit(write) == RingResult::invalid_argument,
            "oversized RawFrame was accepted");
        ringbuf_cancel(write);

        require(
            ringbuf_reserve(ring, write) == RingResult::ok,
            "cancelled Slot was not reusable");
        std::array<std::byte, 16> expected{};
        for (std::size_t index = 0; index < expected.size(); ++index) {
            expected[index] = static_cast<std::byte>(index + 1);
        }
        std::copy(expected.begin(), expected.end(), write.payload().begin());
        write.envelope() = {
            .frame_id = 11,
            .timestamp = 22,
            .type_id = 0x42,
            .type_version = 7,
            .payload_length = static_cast<std::uint32_t>(expected.size()),
            .flags = 3,
        };
        require(
            ringbuf_commit(write) == RingResult::ok,
            "RawFrame commit failed");

        RingReadLease read;
        require(
            ringbuf_acquire(ring, read) == RingResult::ok,
            "committed RawFrame was not readable");
        require(
            read.frame().size() == kSlotHeaderSize + expected.size() &&
                read.envelope().frame_id == 11 &&
                read.envelope().flags == 3 &&
                std::equal(
                    expected.begin(), expected.end(), read.payload().begin()),
            "RawFrame physical layout is incorrect");
        require(
            ringbuf_release(read) == RingResult::ok,
            "could not release RawFrame");

        RingBuffer* opened = ringbuf_open(name.c_str());
        require(
            opened->header->abi_version == 6 &&
                opened->header->type_id == 0x42 &&
                opened->header->type_version == 7,
            "cross-process ABI metadata mismatch");
        ringbuf_close(opened);

        ring->header->abi_version = 5;
        bool old_abi_rejected = false;
        try {
            RingBuffer* invalid = ringbuf_open(name.c_str());
            ringbuf_close(invalid);
        } catch (const std::runtime_error&) {
            old_abi_rejected = true;
        }
        require(old_abi_rejected, "RingBuffer ABI v5 was accepted");
        ring->header->abi_version = kRingAbiVersion;

        ringbuf_shutdown(ring);
        ringbuf_close(ring);
        ring = nullptr;
        ringbuf_unlink(name.c_str());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ringbuf-test: " << error.what() << '\n';
        ringbuf_shutdown(ring);
        ringbuf_close(ring);
        ringbuf_unlink(name.c_str());
        return 1;
    }
}
