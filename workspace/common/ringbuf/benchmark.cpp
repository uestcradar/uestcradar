#include "ringbuf.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t kLatencySampleLimit = 1'000'000;

struct Arguments {
    std::vector<std::size_t> payloads{
        64, 1024, 4096, 65536, 262144, 1048576};
    std::vector<std::uint32_t> slots{2, 4, 8, 16, 64};
    double warmup_seconds{3.0};
    double duration_seconds{15.0};
    unsigned repetitions{3};
    bool json{false};
};

struct Shared {
    std::atomic<bool> ready{false};
    std::atomic<bool> measure{false};
    std::atomic<bool> stop{false};
    std::uint64_t messages{0};
    std::uint64_t latency_count{0};
    long double latency_total_ns{0};
    double consumer_cpu_seconds{0};
    double p50_ns{0};
    double p99_ns{0};
    double read_hot_p50_ns{0};
    double read_hot_p99_ns{0};
};

std::uint64_t monotonic_ns() {
    timespec value{};
    ::clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
}

double process_cpu_seconds() {
    timespec value{};
    ::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value);
    return static_cast<double>(value.tv_sec) +
           static_cast<double>(value.tv_nsec) / 1e9;
}

std::vector<std::size_t> parse_sizes(std::string_view text) {
    std::vector<std::size_t> result;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string token{text.substr(
            start, comma == text.npos ? text.size() - start : comma - start)};
        result.push_back(std::stoull(token));
        if (comma == text.npos) break;
        start = comma + 1;
    }
    return result;
}

Arguments parse(int argc, char* argv[]) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg{argv[index]};
        const auto next = [&]() -> std::string_view {
            if (++index >= argc) throw std::runtime_error("missing value");
            return argv[index];
        };
        if (arg == "--payload-bytes") {
            result.payloads = parse_sizes(next());
        } else if (arg == "--slot-counts") {
            const auto values = parse_sizes(next());
            result.slots.clear();
            for (std::size_t value : values) {
                result.slots.push_back(static_cast<std::uint32_t>(value));
            }
        } else if (arg == "--warmup") {
            result.warmup_seconds = std::stod(std::string{next()});
        } else if (arg == "--duration") {
            result.duration_seconds = std::stod(std::string{next()});
        } else if (arg == "--repetitions") {
            result.repetitions =
                static_cast<unsigned>(std::stoul(std::string{next()}));
        } else if (arg == "--format") {
            result.json = next() == "jsonl";
        } else {
            throw std::runtime_error(
                "usage: ringbuf-benchmark [--payload-bytes LIST] "
                "[--slot-counts LIST] [--warmup SEC] [--duration SEC] "
                "[--repetitions N] [--format table|jsonl]");
        }
    }
    return result;
}

double percentile(std::vector<double>& values, double fraction) {
    std::sort(values.begin(), values.end());
    const std::size_t index = std::min(
        values.size() - 1,
        static_cast<std::size_t>(
            std::ceil(fraction * values.size())) - 1);
    return values[index];
}

void consumer(
    const std::string& name,
    Shared* shared) {
    RingBuffer* ring = ringbuf_open(name.c_str());
    std::vector<double> latency_samples;
    std::vector<double> read_hot_samples;
    latency_samples.reserve(kLatencySampleLimit);
    read_hot_samples.reserve(kLatencySampleLimit);
    bool measuring = false;
    double cpu_started = 0;
    shared->ready.store(true, std::memory_order_release);
    for (;;) {
        if (!measuring &&
            shared->measure.load(std::memory_order_acquire)) {
            measuring = true;
            cpu_started = process_cpu_seconds();
        }
        RingReadLease lease;
        const std::uint64_t hot_started = monotonic_ns();
        const RingResult result = ringbuf_acquire(ring, lease);
        if (result == RingResult::ok) {
            if (measuring) {
                std::uint64_t sent_ns = 0;
                std::memcpy(
                    &sent_ns, lease.payload().data(), sizeof(sent_ns));
                const double latency =
                    static_cast<double>(monotonic_ns() - sent_ns);
                ++shared->messages;
                ++shared->latency_count;
                shared->latency_total_ns += latency;
                if (latency_samples.size() < kLatencySampleLimit) {
                    latency_samples.push_back(latency);
                }
            }
            if (ringbuf_release(lease) != RingResult::ok) {
                ::_exit(2);
            }
            if (measuring &&
                read_hot_samples.size() < kLatencySampleLimit) {
                read_hot_samples.push_back(
                    static_cast<double>(monotonic_ns() - hot_started));
            }
            continue;
        }
        if (result != RingResult::would_block) {
            ::_exit(3);
        }
        if (shared->stop.load(std::memory_order_acquire) &&
            ringbuf_occupied_slots(ring) == 0) {
            break;
        }
        std::this_thread::yield();
    }
    shared->consumer_cpu_seconds =
        process_cpu_seconds() - cpu_started;
    if (!latency_samples.empty()) {
        shared->p50_ns = percentile(latency_samples, 0.50);
        shared->p99_ns = percentile(latency_samples, 0.99);
    }
    if (!read_hot_samples.empty()) {
        shared->read_hot_p50_ns = percentile(read_hot_samples, 0.50);
        shared->read_hot_p99_ns = percentile(read_hot_samples, 0.99);
    }
    ringbuf_close(ring);
}

void produce_until(
    RingBuffer* ring,
    std::size_t payload_bytes,
    Clock::time_point deadline,
    std::vector<double>* hot_samples = nullptr) {
    std::uint64_t sequence = 0;
    while (Clock::now() < deadline) {
        RingWriteLease lease;
        const std::uint64_t hot_started = monotonic_ns();
        if (ringbuf_reserve(ring, lease) != RingResult::ok) {
            std::this_thread::yield();
            continue;
        }
        const std::uint64_t now = monotonic_ns();
        std::memcpy(lease.payload().data(), &now, sizeof(now));
        std::memcpy(
            lease.payload().data() + sizeof(now),
            &sequence,
            sizeof(sequence));
        lease.envelope() = {
            .frame_id = sequence,
            .timestamp = now,
            .type_id = ring->header->type_id,
            .type_version = ring->header->type_version,
            .payload_length = static_cast<std::uint32_t>(payload_bytes),
        };
        if (ringbuf_commit(lease) != RingResult::ok) {
            throw std::runtime_error("benchmark commit failed");
        }
        if (hot_samples != nullptr &&
            hot_samples->size() < kLatencySampleLimit) {
            hot_samples->push_back(
                static_cast<double>(monotonic_ns() - hot_started));
        }
        ++sequence;
    }
}

void run_case(
    std::size_t payload,
    std::uint32_t slots,
    const Arguments& arguments,
    unsigned repetition) {
    if (payload < 16 || payload > INT32_MAX) {
        throw std::runtime_error("benchmark payload must be 16..INT32_MAX");
    }
    const std::string name =
        "/uestcradar_slot_bm_" + std::to_string(::getpid()) + "_" +
        std::to_string(payload) + "_" + std::to_string(slots);
    RingBuffer* ring =
        ringbuf_create(name.c_str(), {slots,
            static_cast<std::uint32_t>(payload), 0xb001, 1});
    auto* shared = static_cast<Shared*>(::mmap(
        nullptr, sizeof(Shared), PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (shared == MAP_FAILED) throw std::runtime_error("mmap failed");
    ::new (shared) Shared{};
    const pid_t child = ::fork();
    if (child == 0) {
        ringbuf_close(ring);
        consumer(name, shared);
        ::_exit(0);
    }
    while (!shared->ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    produce_until(
        ring, payload,
        Clock::now() + std::chrono::duration_cast<Clock::duration>(
                           std::chrono::duration<double>{
                               arguments.warmup_seconds}));
    while (ringbuf_occupied_slots(ring) != 0) {
        std::this_thread::yield();
    }
    const double producer_cpu_started = process_cpu_seconds();
    const auto started = Clock::now();
    std::vector<double> write_hot_samples;
    write_hot_samples.reserve(kLatencySampleLimit);
    shared->measure.store(true, std::memory_order_release);
    produce_until(
        ring, payload,
        started + std::chrono::duration_cast<Clock::duration>(
                      std::chrono::duration<double>{
                          arguments.duration_seconds}),
        &write_hot_samples);
    const auto production_ended = Clock::now();
    shared->stop.store(true, std::memory_order_release);
    int status = 0;
    ::waitpid(child, &status, 0);
    const double wall =
        std::chrono::duration<double>(production_ended - started).count();
    const double producer_cpu =
        process_cpu_seconds() - producer_cpu_started;
    const double write_hot_p50_ns =
        percentile(write_hot_samples, 0.50);
    const double write_hot_p99_ns =
        percentile(write_hot_samples, 0.99);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        shared->messages == 0) {
        throw std::runtime_error("benchmark consumer failed");
    }
    const double mib =
        static_cast<double>(shared->messages) * payload /
        (1024.0 * 1024.0);
    const double mean_us =
        static_cast<double>(
            shared->latency_total_ns / shared->latency_count) / 1000.0;
    if (arguments.json) {
        std::cout
            << "{\"benchmark\":\"ringbuf-slot\",\"payload_bytes\":"
            << payload << ",\"slot_count\":" << slots
            << ",\"repetition\":" << repetition
            << ",\"messages\":" << shared->messages
            << ",\"duration_s\":" << wall
            << ",\"payload_mib_s\":" << mib / wall
            << ",\"messages_s\":" << shared->messages / wall
            << ",\"latency_us_mean\":" << mean_us
            << ",\"latency_us_p50\":" << shared->p50_ns / 1000.0
            << ",\"latency_us_p99\":" << shared->p99_ns / 1000.0
            << ",\"write_hot_us_p50\":" << write_hot_p50_ns / 1000.0
            << ",\"write_hot_us_p99\":" << write_hot_p99_ns / 1000.0
            << ",\"read_hot_us_p50\":"
            << shared->read_hot_p50_ns / 1000.0
            << ",\"read_hot_us_p99\":"
            << shared->read_hot_p99_ns / 1000.0
            << ",\"producer_cpu_pct\":" << producer_cpu / wall * 100.0
            << ",\"consumer_cpu_pct\":"
            << shared->consumer_cpu_seconds / wall * 100.0 << "}\n";
    } else {
        std::cout << std::fixed << std::setprecision(2)
                  << "payload=" << payload << " slots=" << slots
                  << " rep=" << repetition
                  << " MiB/s=" << mib / wall
                  << " msg/s=" << shared->messages / wall
                  << " mean_us=" << mean_us
                  << " p50_us=" << shared->p50_ns / 1000.0
                  << " p99_us=" << shared->p99_ns / 1000.0
                  << " write_hot_p50_us=" << write_hot_p50_ns / 1000.0
                  << " write_hot_p99_us=" << write_hot_p99_ns / 1000.0
                  << " read_hot_p50_us="
                  << shared->read_hot_p50_ns / 1000.0
                  << " read_hot_p99_us="
                  << shared->read_hot_p99_ns / 1000.0
                  << " producer_cpu=" << producer_cpu / wall * 100.0
                  << "% consumer_cpu="
                  << shared->consumer_cpu_seconds / wall * 100.0
                  << "%\n";
    }
    shared->~Shared();
    ::munmap(shared, sizeof(Shared));
    ringbuf_shutdown(ring);
    ringbuf_close(ring);
    ringbuf_unlink(name.c_str());
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Arguments arguments = parse(argc, argv);
        for (std::size_t payload : arguments.payloads) {
            for (std::uint32_t slots : arguments.slots) {
                for (unsigned repetition = 1;
                     repetition <= arguments.repetitions;
                     ++repetition) {
                    run_case(
                        payload, slots, arguments, repetition);
                }
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ringbuf-benchmark: " << error.what() << '\n';
        return 1;
    }
}
