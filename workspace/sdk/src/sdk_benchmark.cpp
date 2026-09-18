#include <data.h>

#include "ringbuf/ringbuf.hpp"

#include <chrono>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>

namespace {

constexpr std::uint64_t kRDTypeId = 3;
constexpr std::uint32_t kRDVersion = 2;
constexpr std::size_t kRDMetadataBytes = 32;
constexpr std::size_t kWarmupFrames = 10000;

std::size_t parse_size(const char* text, const char* name) {
    char* end = nullptr;
    const auto value = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0' || value == 0) {
        throw std::invalid_argument(std::string{"invalid "} + name);
    }
    return static_cast<std::size_t>(value);
}

std::pair<int, int> benchmark_cpus() {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (::sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        return {-1, -1};
    }
    int first = -1;
    int second = -1;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &allowed)) {
            continue;
        }
        if (first < 0) {
            first = cpu;
        } else {
            second = cpu;
            break;
        }
    }
    return {first, second};
}

void pin_current_thread(int cpu) {
    if (cpu < 0) {
        return;
    }
    cpu_set_t selected;
    CPU_ZERO(&selected);
    CPU_SET(cpu, &selected);
    static_cast<void>(::pthread_setaffinity_np(
        ::pthread_self(), sizeof(selected), &selected));
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t requested_payload =
            argc > 1 ? parse_size(argv[1], "payload bytes") : 64 * 1024;
        const std::size_t frame_count =
            argc > 2 ? parse_size(argv[2], "frame count") : 10000;
        if (requested_payload <= kRDMetadataBytes ||
            (requested_payload - kRDMetadataBytes) % sizeof(float) != 0) {
            throw std::invalid_argument(
                "payload bytes must be 32 + a multiple of sizeof(float)");
        }

        const std::string ring_name =
            "/uestcradar_sdk_benchmark_" + std::to_string(::getpid());
        RingBuffer* ring = ringbuf_create(
            ring_name.c_str(),
            {64, static_cast<std::uint32_t>(requested_payload),
             kRDTypeId, kRDVersion});
        ::setenv(
            "UESTCRADAR_DOWNSTREAM_SHM_NAME", ring_name.c_str(), 1);

        const auto [producer_cpu, consumer_cpu] = benchmark_cpus();
        pin_current_thread(producer_cpu);
        std::exception_ptr consumer_error;
        std::atomic<bool> consumer_ready{false};
        std::thread consumer([&] {
            try {
                pin_current_thread(consumer_cpu);
                consumer_ready.store(true, std::memory_order_release);
                for (std::size_t index = 0;
                     index < frame_count + kWarmupFrames; ++index) {
                    RingReadLease lease;
                    while (ringbuf_acquire(ring, lease) == RingResult::would_block) {
                        std::this_thread::yield();
                    }
                    if (!lease.active() ||
                        lease.envelope().payload_length != requested_payload) {
                        throw std::runtime_error("consumer received an invalid frame");
                    }
                    if (ringbuf_release(lease) != RingResult::ok) {
                        throw std::runtime_error("consumer could not release a frame");
                    }
                }
            } catch (...) {
                consumer_error = std::current_exception();
            }
        });

        uestcradar::Output<uestcradar::RDFrame> output;
        const uestcradar::RDMetadata metadata{
            .channel_index = 0,
            .range_bin_count = 1,
            .doppler_bin_count = static_cast<std::uint32_t>(
                (requested_payload - kRDMetadataBytes) / sizeof(float)),
            .range_resolution_m = 1.0,
            .velocity_resolution_mps = 1.0,
        };

        while (!consumer_ready.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t index = 0; index < kWarmupFrames; ++index) {
            auto frame = output.create(metadata);
            frame.data()[0][0] = static_cast<float>(index);
            output.write(std::move(frame));
        }
        const auto started = std::chrono::steady_clock::now();
        for (std::size_t index = 0; index < frame_count; ++index) {
            auto frame = output.create(metadata);
            frame.data()[0][0] = static_cast<float>(index);
            output.write(std::move(frame));
        }
        consumer.join();
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        if (consumer_error) {
            std::rethrow_exception(consumer_error);
        }

        const double mib = static_cast<double>(requested_payload) *
            static_cast<double>(frame_count) / (1024.0 * 1024.0);
        std::cout << "{\"payload_bytes\":" << requested_payload
                  << ",\"frames\":" << frame_count
                  << ",\"seconds\":" << elapsed
                  << ",\"mib_per_second\":" << mib / elapsed
                  << ",\"frames_per_second\":"
                  << static_cast<double>(frame_count) / elapsed << "}\n";

        ringbuf_shutdown(ring);
        ringbuf_close(ring);
        ringbuf_unlink(ring_name.c_str());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk-benchmark: " << error.what() << '\n';
        return 1;
    }
}
