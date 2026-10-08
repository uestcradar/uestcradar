#include "cpi_data.hpp"

#include "forwarder/forwarder.hpp"
#include "network/ucx_transport.hpp"
#include "ringbuf/ringbuf.hpp"

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

class TestRing {
public:
    TestRing(std::string name, const RingBufferConfig& config)
        : name_(std::move(name)),
          ring_(ringbuf_create(name_.c_str(), config)) {}

    ~TestRing() {
        ringbuf_shutdown(ring_);
        ringbuf_close(ring_);
        ringbuf_unlink(name_.c_str());
    }

    RingBuffer* get() const noexcept { return ring_; }
    const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
    RingBuffer* ring_;
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: cpi-sidecar-test <CPI-directory>\n";
            return 2;
        }
        ::setenv("UCX_TLS", "tcp,self", 1);
        const auto cpi = radar_example::load_cpi(argv[1]);
        const std::string prefix =
            "/uestcradar_cpi_sidecar_" + std::to_string(::getpid());
        const RingBufferConfig config{8, 4194304, 1, 3};
        TestRing source{prefix + "_source", config};
        TestRing destination{prefix + "_destination", config};
        ::setenv(
            "UESTCRADAR_DOWNSTREAM_SHM_NAME",
            source.name().c_str(),
            1);
        {
            uestcradar::Output<uestcradar::IQFrame> output;
            auto frame = output.create(cpi.metadata);
            radar_example::copy_cpi_samples(cpi, frame);
            output.write(std::move(frame));
        }

        const auto port = static_cast<std::uint16_t>(
            30000 + (::getpid() % 20000));
        volatile std::sig_atomic_t ingress_running = 1;
        volatile std::sig_atomic_t egress_running = 1;
        sidecar::forwarder::LegMetrics ingress_metrics;
        sidecar::forwarder::LegMetrics egress_metrics;
        std::exception_ptr ingress_error;
        std::exception_ptr egress_error;

        std::thread ingress([&] {
            try {
                auto transport = sidecar::network::UCXTransport::accept_one({
                    "127.0.0.1", port, std::chrono::seconds{10}});
                auto memory = transport.register_memory(
                    ringbuf_storage(destination.get()));
                sidecar::forwarder::run_ingress_session(
                    ingress_running,
                    destination.get(),
                    transport,
                    memory,
                    ingress_metrics);
            } catch (...) {
                ingress_error = std::current_exception();
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        std::thread egress([&] {
            try {
                auto transport = sidecar::network::UCXTransport::connect({
                    "127.0.0.1", port, std::chrono::seconds{10}});
                auto memory = transport.register_memory(
                    ringbuf_storage(source.get()));
                sidecar::forwarder::run_egress_session(
                    egress_running,
                    source.get(),
                    transport,
                    memory,
                    egress_metrics);
            } catch (...) {
                egress_error = std::current_exception();
            }
        });

        RingReadLease received;
        bool acquired = false;
        for (int attempt = 0; attempt < 20000 && !acquired; ++attempt) {
            const auto status = ringbuf_acquire(destination.get(), received);
            if (status == RingResult::ok) {
                acquired = true;
            } else if (status != RingResult::would_block) {
                break;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds{50});
            }
        }
        const bool transfer_ok = acquired &&
            received.envelope().type_id == 1 &&
                received.envelope().type_version == 3 &&
                received.envelope().payload_length == 3006960 &&
                received.payload().size() == 3006960 &&
                std::memcmp(
                    received.payload().data() + 2136,
                    cpi.cs16.data(),
                    cpi.cs16.size()) == 0;
        const bool release_ok = !acquired ||
            ringbuf_release(received) == RingResult::ok;

        ingress_running = 0;
        egress_running = 0;
        ringbuf_shutdown(source.get());
        ringbuf_shutdown(destination.get());
        egress.join();
        ingress.join();
        if (egress_error != nullptr) {
            std::rethrow_exception(egress_error);
        }
        if (ingress_error != nullptr) {
            std::rethrow_exception(ingress_error);
        }
        require(
            transfer_ok && release_ok,
            "Sidecar changed or did not transfer the complete CPI");
        require(
            egress_metrics.payload_bytes_total.load() == 3006960 &&
                ingress_metrics.payload_bytes_total.load() == 3006960,
            "Sidecar full-CPI Goodput counters are incorrect");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cpi-sidecar-test: " << error.what() << '\n';
        return 1;
    }
}
