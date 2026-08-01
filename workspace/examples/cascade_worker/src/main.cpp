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
            const std::uint64_t seed = parse_unsigned(next(), "--seed");
            if (seed > UINT32_MAX) {
                throw std::invalid_argument("--seed is out of range");
            }
            result.seed = static_cast<std::uint32_t>(seed);
        } else if (option == "--warmup-seconds") {
            result.warmup_seconds =
                parse_double(next(), "--warmup-seconds");
        } else if (option == "--duration-seconds") {
            result.duration_seconds =
                parse_double(next(), "--duration-seconds");
        } else if (option == "--rate-mib-s") {
            result.rate_mib_s = parse_double(next(), "--rate-mib-s");
        } else {
            throw std::invalid_argument(
                "usage: cascade-worker --role source|operator|sink "
                "[--test correctness|benchmark] [--payload-bytes N] "
                "[--frames N] [--seed N] [--warmup-seconds N] "
                "[--duration-seconds N] [--rate-mib-s N]");
        }
    }

    if (result.role != "source" && result.role != "operator" &&
        result.role != "sink") {
        throw std::invalid_argument(
            "--role must be source, operator, or sink");
    }
    if (result.test != "correctness" && result.test != "benchmark") {
        throw std::invalid_argument(
            "--test must be correctness or benchmark");
    }
    constexpr std::size_t minimum =
        sizeof(uestcradar::IQMetadata) +
        sizeof(uestcradar::ComplexInt16);
    if (result.payload_bytes < minimum ||
        result.payload_bytes > INT32_MAX || result.frames == 0 ||
        result.duration_seconds <= 0.0) {
        throw std::invalid_argument("cascade-worker arguments are invalid");
    }
    return result;
}

std::uint64_t encode_frame_id(
    std::uint64_t sequence,
    std::uint64_t phase) {
    return (phase << kPhaseShift) | (sequence & kSequenceMask);
}

std::uint64_t phase_from(std::uint64_t frame_id) {
    return frame_id >> kPhaseShift;
}

std::uint64_t sequence_from(std::uint64_t frame_id) {
    return frame_id & kSequenceMask;
}

std::size_t samples_for_payload(std::size_t requested_bytes) {
    return (requested_bytes - sizeof(uestcradar::IQMetadata)) /
           sizeof(uestcradar::ComplexInt16);
}

std::size_t actual_payload_bytes(std::size_t samples) {
    return sizeof(uestcradar::IQMetadata) +
           samples * sizeof(uestcradar::ComplexInt16);
}

std::uint32_t mix(std::uint32_t value) noexcept {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

uestcradar::ComplexInt16 expected_sample(
    std::uint32_t seed,
    std::uint64_t sequence,
    std::size_t index) noexcept {
    const std::uint32_t value = mix(
        seed ^ static_cast<std::uint32_t>(sequence) ^
        static_cast<std::uint32_t>(sequence >> 32) ^
        static_cast<std::uint32_t>(index * 0x9e3779b1U));
    return {
        static_cast<std::int16_t>(value & 0xffffU),
        static_cast<std::int16_t>(value >> 16),
    };
}

void fill_payload(
    uestcradar::IQFrame& frame,
    std::uint32_t seed,
    std::uint64_t sequence,
    bool correctness) {
    auto values = frame.data.values();
    if (!correctness) {
        std::fill(values.begin(), values.end(), uestcradar::ComplexInt16{});
        return;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = expected_sample(seed, sequence, index);
    }
}

void write_frame(
    uestcradar::Output<uestcradar::IQFrame>& output,
    std::size_t samples,
    std::uint64_t sequence,
    std::uint64_t phase,
    const Arguments& arguments) {
    auto frame = output.create({
        .frame_id = encode_frame_id(sequence, phase),
        .timestamp_unix_ns = unix_ns(),
        .channel_count = 1,
        .samples_per_channel = static_cast<std::uint32_t>(samples),
        .sample_rate_hz = 1.0,
        .center_frequency_hz = 0.0,
    });
    fill_payload(
        frame,
        arguments.seed,
        sequence,
        arguments.test == "correctness");
    output.write(frame);
}

void pace(
    double rate_mib_s,
    std::uint64_t bytes_sent,
    std::uint64_t started_ns) {
    if (rate_mib_s <= 0.0) {
        return;
    }
    const double seconds =
        static_cast<double>(bytes_sent) /
        (rate_mib_s * 1024.0 * 1024.0);
    const std::uint64_t target = started_ns +
        static_cast<std::uint64_t>(seconds * 1e9);
    const std::uint64_t now = monotonic_ns();
    if (target > now) {
        std::this_thread::sleep_for(
            std::chrono::nanoseconds{target - now});
    }
}

struct Statistics {
    std::uint64_t frames{0};
    std::uint64_t bytes{0};
    std::uint64_t corrupted{0};
    std::uint64_t missing{0};
    std::uint64_t duplicate{0};
    std::uint64_t reordered{0};
    std::uint64_t last_sequence{0};
    std::uint64_t first_ns{0};
    std::uint64_t last_ns{0};
    std::uint64_t latency_total_ns{0};
    std::vector<std::uint64_t> latency_us;

    explicit Statistics(bool latency)
        : latency_us(latency ? kHistogramBuckets : 0) {}

    bool ok(std::uint64_t expected_frames, bool correctness) const {
        return !correctness ||
               (frames == expected_frames && corrupted == 0 &&
                missing == 0 && duplicate == 0 && reordered == 0);
    }
};

bool validate_frame(
    const uestcradar::IQFrame& frame,
    const Arguments& arguments,
    std::size_t samples,
    Statistics& stats) {
    const std::uint64_t sequence = sequence_from(frame.metadata.frame_id);
    if (stats.last_sequence != 0) {
        if (sequence == stats.last_sequence) {
            ++stats.duplicate;
        } else if (sequence < stats.last_sequence) {
            ++stats.reordered;
        } else if (sequence > stats.last_sequence + 1) {
            stats.missing += sequence - stats.last_sequence - 1;
        }
    } else if (sequence > 1) {
        stats.missing += sequence - 1;
    }
    if (sequence > stats.last_sequence) {
        stats.last_sequence = sequence;
    }

    bool valid =
        frame.metadata.channel_count == 1 &&
        frame.metadata.samples_per_channel == samples &&
        frame.data.rows() == 1 && frame.data.columns() == samples;
    if (valid) {
        const auto values = frame.data.values();
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto expected = expected_sample(
                arguments.seed, sequence, index);
            if (values[index].i != expected.i ||
                values[index].q != expected.q) {
                valid = false;
                break;
            }
        }
    }
    if (!valid) {
        ++stats.corrupted;
    }
    return valid;
}

std::uint64_t percentile(
    const std::vector<std::uint64_t>& histogram,
    std::uint64_t samples,
    double fraction) {
    if (histogram.empty() || samples == 0) {
        return 0;
    }
    const std::uint64_t target = static_cast<std::uint64_t>(
        std::ceil(static_cast<double>(samples) * fraction));
    std::uint64_t accumulated = 0;
    for (std::size_t index = 0; index < histogram.size(); ++index) {
        accumulated += histogram[index];
        if (accumulated >= target) {
            return index;
        }
    }
    return histogram.size() - 1;
}

void observe(
    const uestcradar::IQFrame& frame,
    std::size_t payload_bytes,
    bool measure_latency,
    Statistics& stats) {
    const std::uint64_t now = monotonic_ns();
    if (stats.frames == 0) {
        stats.first_ns = now;
    }
    stats.last_ns = now;
    ++stats.frames;
    stats.bytes += payload_bytes;
    if (measure_latency) {
        const std::uint64_t wall_now = unix_ns();
        const std::uint64_t latency =
            wall_now >= frame.metadata.timestamp_unix_ns
                ? wall_now - frame.metadata.timestamp_unix_ns
                : 0;
        stats.latency_total_ns += latency;
        ++stats.latency_us[std::min<std::size_t>(
            latency / 1000, stats.latency_us.size() - 1)];
    }
}

void print_statistics(
    const Arguments& arguments,
    const Statistics& stats,
    double cpu_seconds) {
    const double wall = stats.frames > 1
        ? std::max(
              1e-9,
              static_cast<double>(stats.last_ns - stats.first_ns) / 1e9)
        : 1e-9;
    const double mib =
        static_cast<double>(stats.bytes) / (1024.0 * 1024.0);
    std::cout << "{\"benchmark\":\"cascade-" << arguments.test
              << "\",\"role\":\"" << arguments.role
              << "\",\"window\":1"
              << ",\"frames\":" << stats.frames
              << ",\"bytes\":" << stats.bytes
              << ",\"duration_s\":" << wall
              << ",\"mib_s\":" << mib / wall
              << ",\"messages_s\":" << stats.frames / wall
              << ",\"cpu_pct\":" << cpu_seconds / wall * 100.0
              << ",\"corrupted\":" << stats.corrupted
              << ",\"missing\":" << stats.missing
              << ",\"duplicate\":" << stats.duplicate
              << ",\"reordered\":" << stats.reordered;
    if (!stats.latency_us.empty() && stats.frames != 0) {
        std::cout << ",\"latency_us_mean\":"
                  << static_cast<double>(stats.latency_total_ns) /
                         stats.frames / 1000.0
                  << ",\"latency_us_p50\":"
                  << percentile(stats.latency_us, stats.frames, 0.50)
                  << ",\"latency_us_p99\":"
                  << percentile(stats.latency_us, stats.frames, 0.99);
    }
    std::cout << "}\n";
}

int run_source(const Arguments& arguments) {
    uestcradar::Output<uestcradar::IQFrame> output;
    const std::size_t samples = samples_for_payload(arguments.payload_bytes);
    const std::size_t bytes = actual_payload_bytes(samples);
    std::uint64_t sequence = 0;
    Statistics stats{false};

    if (arguments.test == "benchmark") {
        const std::uint64_t warmup_end = monotonic_ns() +
            static_cast<std::uint64_t>(arguments.warmup_seconds * 1e9);
        while (monotonic_ns() < warmup_end) {
            write_frame(output, samples, ++sequence, kWarmup, arguments);
        }
    }

    const double cpu_started = process_cpu_seconds();
    const std::uint64_t started = monotonic_ns();
    const std::uint64_t deadline = started +
        static_cast<std::uint64_t>(arguments.duration_seconds * 1e9);
    while ((arguments.test == "correctness" &&
            stats.frames < arguments.frames) ||
           (arguments.test == "benchmark" &&
            monotonic_ns() < deadline)) {
        write_frame(output, samples, ++sequence, kMeasure, arguments);
        if (stats.frames == 0) {
            stats.first_ns = monotonic_ns();
        }
        stats.last_ns = monotonic_ns();
        ++stats.frames;
        stats.bytes += bytes;
        pace(arguments.rate_mib_s, stats.bytes, started);
    }
    const double cpu = process_cpu_seconds() - cpu_started;
    write_frame(output, samples, ++sequence, kEnd, arguments);
    print_statistics(arguments, stats, cpu);
    return 0;
}

int run_receiver(const Arguments& arguments) {
    uestcradar::Input<uestcradar::IQFrame> input;
    const bool is_operator = arguments.role == "operator";
    const bool correctness = arguments.test == "correctness";
    const std::size_t samples = samples_for_payload(arguments.payload_bytes);
    const std::size_t bytes = actual_payload_bytes(samples);
    Statistics stats{arguments.role == "sink"};
    double cpu_started = 0.0;
    bool measurement_started = false;

    if (is_operator) {
        uestcradar::Output<uestcradar::IQFrame> output;
        for (;;) {
            auto input_frame = input.read();
            const std::uint64_t phase =
                phase_from(input_frame.metadata.frame_id);
            if (phase == kMeasure) {
                if (!measurement_started) {
                    cpu_started = process_cpu_seconds();
                    measurement_started = true;
                }
                if (correctness) {
                    static_cast<void>(validate_frame(
                        input_frame, arguments, samples, stats));
                }
                observe(input_frame, bytes, false, stats);
            }

            auto output_frame = output.create(input_frame.metadata);
            std::copy(
                input_frame.data.values().begin(),
                input_frame.data.values().end(),
                output_frame.data.values().begin());
            output.write(output_frame);
            if (phase == kEnd) {
                break;
            }
        }
    } else {
        for (;;) {
            auto frame = input.read();
            const std::uint64_t phase = phase_from(frame.metadata.frame_id);
            if (phase == kEnd) {
                break;
            }
            if (phase != kMeasure) {
                continue;
            }
            if (!measurement_started) {
                cpu_started = process_cpu_seconds();
                measurement_started = true;
            }
            if (correctness) {
                static_cast<void>(validate_frame(
                    frame, arguments, samples, stats));
            }
            observe(frame, bytes, true, stats);
        }
    }

    const double cpu = measurement_started
        ? process_cpu_seconds() - cpu_started
        : 0.0;
    print_statistics(arguments, stats, cpu);
    return stats.ok(arguments.frames, correctness) ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        return arguments.role == "source"
                   ? run_source(arguments)
                   : run_receiver(arguments);
    } catch (const std::exception& error) {
        std::cerr << "cascade-worker: " << error.what() << '\n';
        return 1;
    }
}
