#include "raw_output.hpp"
#include "ringbuf/ringbuf.hpp"
#include <cassert>
#include <unistd.h>

namespace {
struct Ring {
    std::string name = "/raw_output_test_" + std::to_string(getpid());
    RingBuffer* ring = ringbuf_create(name.c_str(), {2, 1024, 4, 1});
    Ring() {
        setenv("UESTCRADAR_DOWNSTREAM_SHM_NAME", name.c_str(), 1);
        setenv("UESTCRADAR_UPSTREAM_SHM_NAME", name.c_str(), 1);
    }
    ~Ring() { ringbuf_close(ring); ringbuf_unlink(name.c_str()); }
};
raw_iq::Block block(std::uint64_t n) { return {{UINT64_MAX, n, 1, 2}, {{1,-2},{3,-4}}}; }
template<class Predicate> void wait(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}
int main() {
    {
        Ring ring;
        for (int i=0; i<100; ++i) {
            try { pcie_source::RawOutput output(2, CPU_SETSIZE); assert(false); }
            catch (const std::invalid_argument&) {}
        }
    }
    {
        Ring ring;
        pcie_source::RawOutput output(2);
        uestcradar::Input<uestcradar::RawIQFrame> input;
        raw_iq::Digest expected;
        for (std::uint64_t n = 0; n < 5; ++n) {
            const auto value = block(n);
            expected.add(value.metadata, value.samples);
            output.push(value);
            const auto received = input.read();
            assert(received.metadata().rx_timestamp == n && received.metadata().tx_timestamp == UINT64_MAX);
            assert(received.data()[0][1].q == -4);
        }
        const auto result = output.finish();
        assert(result.frames == 5 && result.value == expected.value && output.sent() == 5);
    }
    {
        Ring ring;
        const auto start = std::chrono::steady_clock::now();
        {
            pcie_source::RawOutput output(2);
            output.push(block(0)); output.push(block(1));
            wait([&] { return output.sent() == 2; });
            output.push(block(2)); output.push(block(3));
            bool full = false;
            try { output.push(block(4)); } catch (const std::runtime_error&) { full = true; }
            assert(full); // No reader: bounded memory, no hidden drop or overwrite.
        } // Cancels the worker even when SDK Ring is full.
        assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    }
    {
        Ring ring;
        pcie_source::RawOutput output(2);
        output.push(block(0)); output.push(block(1));
        wait([&] { return output.sent() == 2; });
        output.push(block(2));
        ringbuf_shutdown(ring.ring);
        bool failed = false;
        wait([&] {
            try { output.check(); } catch (const std::runtime_error&) { failed = true; }
            return failed;
        });
        try { output.finish(); assert(false); } catch (const std::runtime_error&) {}
    }
    {
        Ring ring;
        pcie_source::RawOutput output(2);
        output.push(block(0)); output.push(block(1));
        wait([&] { return output.sent() == 2; });
        output.push(block(2));
        const auto start = std::chrono::steady_clock::now();
        try { output.finish(); assert(false); } catch (const std::runtime_error&) {}
        assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(6));
    }
}
