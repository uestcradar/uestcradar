#include <data.h>
#include <sdk.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <time.h>

namespace {

constexpr std::uint64_t kPhaseShift = 62;
constexpr std::uint64_t kSequenceMask =
    (std::uint64_t{1} << kPhaseShift) - 1;
constexpr std::uint64_t kWarmup = 0;
constexpr std::uint64_t kMeasure = 1;
constexpr std::uint64_t kEnd = 2;
constexpr std::size_t kHistogramBuckets = 1'000'002;

struct Arguments {
    std::string role;
    std::string test{"correctness"};
    std::size_t payload_bytes{64 * 1024};
    std::uint64_t frames{10'000};
    std::uint32_t seed{0x13579bdfU};
    double warmup_seconds{3.0};
    double duration_seconds{30.0};
    double rate_mib_s{0.0};
};

std::uint64_t clock_ns(clockid_t clock) {
    struct timespec value {};
    if (::clock_gettime(clock, &value) != 0) {
        throw std::runtime_error("clock_gettime failed");
    }
    return static_cast<std::uint64_t>(value.tv_sec) *
               1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
}

std::uint64_t monotonic_ns() {
    return clock_ns(CLOCK_MONOTONIC_RAW);
}

std::uint64_t unix_ns() {
    return clock_ns(CLOCK_REALTIME);
}

double process_cpu_seconds() {
    return static_cast<double>(
               clock_ns(CLOCK_PROCESS_CPUTIME_ID)) /
           1e9;
}

std::uint64_t parse_unsigned(const char* value, const char* option) {
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(value, &end, 0);
    if (errno != 0 || end == value || *end != '\0') {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return parsed;
}

double parse_double(const char* value, const char* option) {
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(value, &end);
    if (errno != 0 || end == value || *end != '\0' ||
        !std::isfinite(parsed) || parsed < 0.0) {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return parsed;
}

Arguments parse_arguments(int argc, char* argv[]) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option{argv[index]};
        const auto next = [&]() -> const char* {
            if (++index >= argc) {
                throw std::invalid_argument(
                    std::string{"missing value for "} +
                    std::string{option});
            }
            return argv[index];
        };

        if (option == "--role") {
            result.role = next();
        } else if (option == "--test") {
            result.test = next();
        } else if (option == "--payload-bytes") {
            result.payload_bytes = static_cast<std::size_t>(
                parse_unsigned(next(), "--payload-bytes"));
        } else if (option == "--frames") {
            result.frames = parse_unsigned(next(), "--frames");
        } else if (option == "--seed") {
            result.seed = static_cast<std::uint32_t>(
                parse_unsigned(next(), "--seed"));
        } else if (option == "--warmup-seconds") {
            result.warmup_seconds = parse_double(next(), "--warmup-seconds");
        } else if (option == "--duration-seconds") {
            result.duration_seconds = parse_double(next(), "--duration-seconds");
        } else if (option == "--rate-mib-s") {
            result.rate_mib_s = parse_double(next(), "--rate-mib-s");
        } else {
            throw std::invalid_argument(
                std::string{"unknown option "} + std::string{option});
        }
    }

    if (result.role != "source" && result.role != "operator" &&
        result.role != "sink") {
        throw std::invalid_argument(
            "--role must be 'source', 'operator', or 'sink'");
    }

    if (result.test != "correctness" && result.test != "benchmark") {
        throw std::invalid_argument(
            "--test must be 'correctness' or 'benchmark'");
    }

    if (result.payload_bytes < 16) {
        throw std::invalid_argument("--payload-bytes must be >= 16");
    }

    return result;
}

std::uint32_t mix_u32(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

std::uint8_t generate_byte(
    std::uint32_t seed,
    std::uint64_t sequence,
    std::size_t offset) {
    const std::uint32_t key =
        seed ^
        mix_u32(static_cast<std::uint32_t>(sequence)) ^
        mix_u32(static_cast<std::uint32_t>(offset));
    return static_cast<std::uint8_t>(key & 0xffU);
}

void write_u64_le(std::uint8_t* buffer, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        buffer[index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xffU);
    }
}

std::uint64_t read_u64_le(const std::uint8_t* buffer) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(buffer[index]) << (index * 8);
    }
    return value;
}

void fill_payload(
    std::uint8_t* buffer,
    std::size_t size,
    std::uint32_t seed,
    std::uint64_t header_bits) {
    write_u64_le(buffer, header_bits);
    write_u64_le(buffer + 8, unix_ns());
    const std::uint64_t sequence = header_bits & kSequenceMask;
    for (std::size_t index = 16; index < size; ++index) {
        buffer[index] = generate_byte(seed, sequence, index);
    }
}

void verify_payload(
    const std::uint8_t* buffer,
    std::size_t size,
    std::uint32_t seed,
    std::uint64_t sequence) {
    for (std::size_t index = 16; index < size; ++index) {
        const std::uint8_t expected = generate_byte(seed, sequence, index);
        if (buffer[index] != expected) {
            throw std::runtime_error(
                "payload corruption at index " + std::to_string(index) +
                ": expected " + std::to_string(static_cast<int>(expected)) +
                ", got " + std::to_string(static_cast<int>(buffer[index])));
        }
    }
}

void pacing_sleep(std::uint64_t start_ns, std::uint64_t sent_bytes, double rate_mib_s) {
    if (rate_mib_s <= 0.0) {
        return;
    }

    const double target_seconds =
        static_cast<double>(sent_bytes) / (rate_mib_s * 1024.0 * 1024.0);
    const std::uint64_t target_ns =
        start_ns + static_cast<std::uint64_t>(target_seconds * 1e9);
    const std::uint64_t now_ns = monotonic_ns();

    if (now_ns < target_ns) {
        const std::uint64_t sleep_ns = target_ns - now_ns;
        if (sleep_ns > 50'000) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns - 20'000));
        }
        while (monotonic_ns() < target_ns) {
            std::this_thread::yield();
        }
    }
}

void run_source(const Arguments& args) {
    const char* shm_name = std::getenv("UESTCRADAR_DOWNSTREAM_SHM_NAME");
    shm_name = (shm_name != nullptr && shm_name[0] != '\0')
                   ? shm_name
                   : "/uestcradar_cascade_a_downstream";

    std::cout << "[source] attaching to downstream SHM: " << shm_name
              << std::endl;

    uestcradar_shm_t* shm = uestcradar_shm_attach_downstream(shm_name);
    if (shm == nullptr) {
        throw std::runtime_error("failed to attach downstream SHM");
    }

    struct Cleanup {
        uestcradar_shm_t* handle;
        ~Cleanup() { uestcradar_shm_detach(handle); }
    } cleanup{shm};

    const std::uint64_t start_time_ns = monotonic_ns();
    std::uint64_t sent_bytes = 0;

    if (args.test == "correctness") {
        for (std::uint64_t seq = 0; seq < args.frames; ++seq) {
            const std::uint64_t header = (kMeasure << kPhaseShift) | seq;
            void* slot_ptr = nullptr;
            const int rc = uestcradar_shm_acquire_output(shm, &slot_ptr, args.payload_bytes);
            if (rc != 0) {
                throw std::runtime_error("failed to acquire output slot");
            }
            fill_payload(
                static_cast<std::uint8_t*>(slot_ptr),
                args.payload_bytes,
                args.seed,
                header);
            uestcradar_shm_commit_output(shm, args.payload_bytes);
            sent_bytes += args.payload_bytes;
            pacing_sleep(start_time_ns, sent_bytes, args.rate_mib_s);
        }
        std::cout << "[source] successfully sent " << args.frames << " frames" << std::endl;
    } else {
        // benchmark mode
        const std::uint64_t warmup_ns = static_cast<std::uint64_t>(args.warmup_seconds * 1e9);
        const std::uint64_t duration_ns = static_cast<std::uint64_t>(args.duration_seconds * 1e9);
        const std::uint64_t end_ns = start_time_ns + warmup_ns + duration_ns;

        std::uint64_t seq = 0;
        while (monotonic_ns() < end_ns) {
            const std::uint64_t now_ns = monotonic_ns();
            std::uint64_t phase = kMeasure;
            if (now_ns < start_time_ns + warmup_ns) {
                phase = kWarmup;
            }

            const std::uint64_t header = (phase << kPhaseShift) | (seq & kSequenceMask);
            void* slot_ptr = nullptr;
            const int rc = uestcradar_shm_acquire_output(shm, &slot_ptr, args.payload_bytes);
            if (rc != 0) {
                throw std::runtime_error("failed to acquire output slot");
            }
            fill_payload(
                static_cast<std::uint8_t*>(slot_ptr),
                args.payload_bytes,
                args.seed,
                header);
            uestcradar_shm_commit_output(shm, args.payload_bytes);
            sent_bytes += args.payload_bytes;
            pacing_sleep(start_time_ns, sent_bytes, args.rate_mib_s);
            seq++;
        }

        // send end frame
        const std::uint64_t end_header = (kEnd << kPhaseShift) | (seq & kSequenceMask);
        void* slot_ptr = nullptr;
        if (uestcradar_shm_acquire_output(shm, &slot_ptr, args.payload_bytes) == 0) {
            fill_payload(static_cast<std::uint8_t*>(slot_ptr), args.payload_bytes, args.seed, end_header);
            uestcradar_shm_commit_output(shm, args.payload_bytes);
        }
        std::cout << "[source] benchmark finished, sent " << seq << " frames" << std::endl;
    }
}

void run_operator(const Arguments& args) {
    const char* up_shm = std::getenv("UESTCRADAR_UPSTREAM_SHM_NAME");
    up_shm = (up_shm != nullptr && up_shm[0] != '\0') ? up_shm : "/uestcradar_cascade_b_upstream";

    const char* down_shm = std::getenv("UESTCRADAR_DOWNSTREAM_SHM_NAME");
    down_shm = (down_shm != nullptr && down_shm[0] != '\0') ? down_shm : "/uestcradar_cascade_b_downstream";

    std::cout << "[operator] upstream: " << up_shm << ", downstream: " << down_shm << std::endl;

    uestcradar_shm_t* in_shm = uestcradar_shm_attach_upstream(up_shm);
    if (in_shm == nullptr) {
        throw std::runtime_error("failed to attach upstream SHM");
    }

    uestcradar_shm_t* out_shm = uestcradar_shm_attach_downstream(down_shm);
    if (out_shm == nullptr) {
        uestcradar_shm_detach(in_shm);
        throw std::runtime_error("failed to attach downstream SHM");
    }

    struct Cleanup {
        uestcradar_shm_t* in;
        uestcradar_shm_t* out;
        ~Cleanup() {
            uestcradar_shm_detach(in);
            uestcradar_shm_detach(out);
        }
    } cleanup{in_shm, out_shm};

    std::uint64_t forwarded_count = 0;
    for (;;) {
        const void* in_ptr = nullptr;
        std::size_t in_len = 0;
        const int rc = uestcradar_shm_read_input(in_shm, &in_ptr, &in_len);
        if (rc != 0) {
            break;
        }

        const auto* buf = static_cast<const std::uint8_t*>(in_ptr);
        const std::uint64_t header = read_u64_le(buf);
        const std::uint64_t phase = header >> kPhaseShift;
        const std::uint64_t seq = header & kSequenceMask;

        if (args.test == "correctness") {
            verify_payload(buf, in_len, args.seed, seq);
        }

        void* out_ptr = nullptr;
        if (uestcradar_shm_acquire_output(out_shm, &out_ptr, in_len) != 0) {
            uestcradar_shm_release_input(in_shm);
            throw std::runtime_error("operator failed to acquire output slot");
        }

        std::copy_n(buf, in_len, static_cast<std::uint8_t*>(out_ptr));
        uestcradar_shm_commit_output(out_shm, in_len);
        uestcradar_shm_release_input(in_shm);

        forwarded_count++;

        if (args.test == "correctness" && forwarded_count >= args.frames) {
            break;
        }
        if (args.test == "benchmark" && phase == kEnd) {
            break;
        }
    }

    std::cout << "[operator] finished forwarding " << forwarded_count << " frames" << std::endl;
}

void run_sink(const Arguments& args) {
    const char* up_shm = std::getenv("UESTCRADAR_UPSTREAM_SHM_NAME");
    up_shm = (up_shm != nullptr && up_shm[0] != '\0') ? up_shm : "/uestcradar_cascade_c_upstream";

    std::cout << "[sink] attaching to upstream SHM: " << up_shm << std::endl;

    uestcradar_shm_t* shm = uestcradar_shm_attach_upstream(up_shm);
    if (shm == nullptr) {
        throw std::runtime_error("failed to attach upstream SHM");
    }

    struct Cleanup {
        uestcradar_shm_t* handle;
        ~Cleanup() { uestcradar_shm_detach(handle); }
    } cleanup{shm};

    std::uint64_t received_count = 0;
    for (;;) {
        const void* in_ptr = nullptr;
        std::size_t in_len = 0;
        const int rc = uestcradar_shm_read_input(shm, &in_ptr, &in_len);
        if (rc != 0) {
            break;
        }

        const auto* buf = static_cast<const std::uint8_t*>(in_ptr);
        const std::uint64_t header = read_u64_le(buf);
        const std::uint64_t phase = header >> kPhaseShift;
        const std::uint64_t seq = header & kSequenceMask;

        if (args.test == "correctness") {
            verify_payload(buf, in_len, args.seed, seq);
        }

        uestcradar_shm_release_input(shm);
        received_count++;

        if (args.test == "correctness" && received_count >= args.frames) {
            break;
        }
        if (args.test == "benchmark" && phase == kEnd) {
            break;
        }
    }

    std::cout << "[sink] finished receiving " << received_count << " frames" << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const Arguments args = parse_arguments(argc, argv);
        if (args.role == "source") {
            run_source(args);
        } else if (args.role == "operator") {
            run_operator(args);
        } else if (args.role == "sink") {
            run_sink(args);
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << std::endl;
        return 1;
    }
}
