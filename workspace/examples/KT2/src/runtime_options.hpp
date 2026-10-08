#pragma once

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

namespace radar_example {

struct RuntimeOptions {
    std::uint64_t frames{0};
    std::uint64_t log_every{10};

    static RuntimeOptions parse(int argc, char** argv) {
        RuntimeOptions options;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument{argv[index]};
            if (argument == "--frames" && index + 1 < argc) {
                options.frames = parse_uint64(argv[++index]);
            } else if (argument == "--log-every" && index + 1 < argc) {
                options.log_every = parse_uint64(argv[++index]);
            } else {
                throw std::invalid_argument(
                    "unknown or incomplete command-line option");
            }
        }
        if (options.log_every == 0) {
            throw std::invalid_argument("log interval must be positive");
        }
        return options;
    }

private:
    static std::uint64_t parse_uint64(const char* value) {
        char* end = nullptr;
        const auto result = std::strtoull(value, &end, 10);
        if (end == value || *end != '\0') {
            throw std::invalid_argument("invalid integer option");
        }
        return result;
    }
};

}  // namespace radar_example
