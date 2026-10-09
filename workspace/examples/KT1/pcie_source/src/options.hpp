#pragma once
#include "cpu_affinity.hpp"
#include <cstdlib>
#include <charconv>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace pcie_source {
struct Options {
    CpuMap cpus{8, 9, 10};
    unsigned channel{};
    unsigned duration_seconds{};
    unsigned queue_frames{512};
    bool capture_only{}, help{};
    std::filesystem::path timestamp_errors{"rx_timestamp_errors.jsonl"};
    std::filesystem::path config_dir{"/app/pcie_config"};
};

inline Options parse_options(int argc, char** argv) {
    Options result;
    std::string_view cpu_map = "8,9,10";
    if (const char* configured = std::getenv("PCIE_CPU_MAP")) cpu_map = configured;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--capture-only") result.capture_only = true;
        else if (option == "--help") result.help = true;
        else {
            if (i + 1 == argc) throw std::invalid_argument("missing option value");
            const std::string_view value(argv[++i]);
            if (option == "--cpu-map") {
                cpu_map = value;
            } else if (option == "--timestamp-errors") {
                if (value.empty()) throw std::invalid_argument("empty timestamp error path");
                result.timestamp_errors = value;
            } else if (option == "--pcie-config-dir") {
                if (value.empty()) throw std::invalid_argument("empty directory");
                result.config_dir = value;
            } else if (option == "--channel" || option == "--duration-seconds" || option == "--queue-frames") {
                unsigned number{};
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                    throw std::invalid_argument("invalid unsigned option value");
                if (option == "--channel") result.channel = number;
                else if (option == "--queue-frames") result.queue_frames = number;
                else result.duration_seconds = number;
            } else throw std::invalid_argument("unknown option");
        }
    }
    result.cpus = parse_cpu_map(cpu_map);
    if (result.channel >= 8) throw std::invalid_argument("channel must be 0..7");
    if (!result.queue_frames || result.queue_frames > 32768)
        throw std::invalid_argument("queue-frames must be 1..32768");
    return result;
}
} // namespace pcie_source
