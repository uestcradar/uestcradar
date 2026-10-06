#pragma once
#include <charconv>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace pcie_source {
struct Options {
    unsigned channel{};
    unsigned duration_seconds{};
    bool capture_only{}, help{};
    std::filesystem::path timestamp_errors{"rx_timestamp_errors.jsonl"};
    std::filesystem::path data_root{"/data"};
    std::filesystem::path config_dir{"/app/pcie_config"};
};

inline Options parse_options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--capture-only") result.capture_only = true;
        else if (option == "--help") result.help = true;
        else {
            if (i + 1 == argc) throw std::invalid_argument("missing option value");
            const std::string_view value(argv[++i]);
            if (option == "--timestamp-errors") {
                if (value.empty()) throw std::invalid_argument("empty timestamp error path");
                result.timestamp_errors = value;
            } else if (option == "--data-root" || option == "--pcie-config-dir") {
                if (value.empty()) throw std::invalid_argument("empty directory");
                (option == "--data-root" ? result.data_root : result.config_dir) = value;
            } else if (option == "--channel" || option == "--duration-seconds") {
                unsigned number{};
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                    throw std::invalid_argument("invalid unsigned option value");
                if (option == "--channel") result.channel = number;
                else result.duration_seconds = number;
            } else throw std::invalid_argument("unknown option");
        }
    }
    if (result.channel >= 8) throw std::invalid_argument("channel must be 0..7");
    return result;
}
} // namespace pcie_source
