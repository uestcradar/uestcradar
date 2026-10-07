#include "forwarder/forwarder.hpp"
#include "ringbuf/ringbuf.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

class TestRing {
public:
    explicit TestRing(std::string name)
        : name_(std::move(name)),
          ring_(ringbuf_create(name_.c_str(), {4, 4096, 1, 1})) {}

    ~TestRing() {
        ringbuf_shutdown(ring_);
        ringbuf_close(ring_);
        ringbuf_unlink(name_.c_str());
    }

    RingBuffer* get() const noexcept {
        return ring_;
    }

private:
    std::string name_;
    RingBuffer* ring_;
};

bool append(RingBuffer* ring, std::size_t bytes) {
    RingWriteLease lease;
    if (ringbuf_reserve(ring, lease) != RingResult::ok) {
        return false;
    }
    for (std::size_t index = 0; index < bytes; ++index) {
        lease.payload()[index] = static_cast<std::byte>(index & 0xff);
    }
    lease.envelope() = {
        .frame_id = bytes,
        .type_id = ring->header->type_id,
        .type_version = ring->header->type_version,
        .payload_length = static_cast<std::uint32_t>(bytes),
    };
    return ringbuf_commit(lease) == RingResult::ok;
}

}  // namespace

int main() {
    TestRing ring{
        "/uestcradar_leg_lifecycle_test_" + std::to_string(::getpid())};
    if (!append(ring.get(), 128) ||
        !append(ring.get(), 1024) ||
        !append(ring.get(), 4096)) {
        std::cerr << "leg-lifecycle-test: could not seed output ring\n";
        return 1;
    }

    const sidecar::forwarder::DroppedFrames dropped =
        sidecar::forwarder::drop_stale_frames(ring.get());
    if (dropped.frames != 3 || dropped.bytes != 5248 ||
        ringbuf_occupied_slots(ring.get()) != 0) {
        std::cerr << "leg-lifecycle-test: stale frame accounting failed\n";
        return 1;
    }
    const auto empty = sidecar::forwarder::drop_stale_frames(ring.get());
    if (empty.frames != 0 || empty.bytes != 0) {
        std::cerr << "leg-lifecycle-test: empty ring was not stable\n";
        return 1;
    }
    return 0;
}
