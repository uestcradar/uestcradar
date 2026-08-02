#include "cpi_data.hpp"

#include "ringbuf/ringbuf.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

std::uint64_t hash_bytes(const std::byte* data, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= std::to_integer<std::uint8_t>(data[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

int main(int argc, char** argv) {
    RingBuffer* ring = nullptr;
    std::string name;
    try {
        if (argc != 2) {
            std::cerr << "usage: cpi-ring-test <CPI-directory>\n";
            return 2;
        }
        const auto cpi = radar_example::load_cpi(argv[1]);
        name = "/uestcradar_cpi_v3_" + std::to_string(::getpid());
        ring = ringbuf_create(name.c_str(), {2, 4194304, 1, 3});
        ::setenv("UESTCRADAR_DOWNSTREAM_SHM_NAME", name.c_str(), 1);
        uestcradar::Output<uestcradar::IQFrame> output;
        auto frame = output.create(cpi.metadata);
        radar_example::copy_cpi_samples(cpi, frame);
        output.write(std::move(frame));

        RingReadLease lease;
        require(
            ringbuf_acquire(ring, lease) == RingResult::ok,
            "cannot acquire complete CPI frame");
        require(
            lease.envelope().type_id == 1 &&
                lease.envelope().type_version == 3 &&
                lease.envelope().payload_length == 3006960 &&
                lease.payload().size() == 3006960,
            "complete CPI Envelope is incorrect");
        const auto* wire_cs16 = lease.payload().data() + 2136;
        require(
            std::memcmp(
                wire_cs16,
                cpi.cs16.data(),
                cpi.cs16.size()) == 0 &&
                hash_bytes(wire_cs16, cpi.cs16.size()) ==
                    hash_bytes(cpi.cs16.data(), cpi.cs16.size()),
            "CS16 bytes changed in the IQFrame Ring slot");
        require(
            ringbuf_release(lease) == RingResult::ok,
            "cannot release complete CPI frame");

        ringbuf_shutdown(ring);
        ringbuf_close(ring);
        ring = nullptr;
        ringbuf_unlink(name.c_str());
        return 0;
    } catch (const std::exception& error) {
        if (ring != nullptr) {
            ringbuf_shutdown(ring);
            ringbuf_close(ring);
        }
        if (!name.empty()) {
            ringbuf_unlink(name.c_str());
        }
        std::cerr << "cpi-ring-test: " << error.what() << '\n';
        return 1;
    }
}
