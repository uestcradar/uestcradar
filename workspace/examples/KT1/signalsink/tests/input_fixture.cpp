#include "ringbuf/ringbuf.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    RingBuffer* ring = ringbuf_create(argv[1], {8, 1024, 999, 1});
    std::cout << "ready" << std::endl;
    std::string command;
    std::uint64_t id = 0;
    while (std::cin >> command) {
        if (command == "push") {
            RingWriteLease lease;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (ringbuf_reserve(ring, lease) == RingResult::would_block && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            if (!lease.active()) return 3;
            lease.envelope() = {.frame_id = id, .timestamp = 1000 + id, .type_id = 999,
                                .type_version = 1, .payload_length = 13, .flags = 0x1234};
            std::fill(std::begin(lease.envelope().reserved), std::end(lease.envelope().reserved), std::byte{0xab});
            std::fill_n(lease.payload().begin(), 13, std::byte(id & 255));
            ++id;
            if (ringbuf_commit(lease) != RingResult::ok) return 4;
            std::cout << "pushed" << std::endl;
        } else if (command == "used") std::cout << ringbuf_occupied_slots(ring) << std::endl;
        else break;
    }
    ringbuf_shutdown(ring);
    ringbuf_close(ring);
    ringbuf_unlink(argv[1]);
}
