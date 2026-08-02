#include <data.h>

#include "rd_verifier.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

struct Options {
    std::uint64_t frames{0};
    std::uint64_t log_every{1};
};

std::uint64_t parse_uint64(const char* value) {
    char* end = nullptr;
    const auto result = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        throw std::invalid_argument("invalid integer option");
    }
    return result;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--frames" && index + 1 < argc) {
            options.frames = parse_uint64(argv[++index]);
        } else if (argument == "--log-every" && index + 1 < argc) {
            options.log_every = parse_uint64(argv[++index]);
        } else {
            throw std::invalid_argument("unknown or incomplete option");
        }
    }
    if (options.log_every == 0) {
        throw std::invalid_argument("log interval must be positive");
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const auto options = parse_options(argc, argv);
        uestcradar::Input<uestcradar::RDFrame> input;
        std::cout << "[rd-sink] waiting for RDFrame v2" << '\n';

        for (std::uint64_t received = 1;
             options.frames == 0 || received <= options.frames;
             ++received) {
            auto frame = input.read();
            const auto data = frame.data();
            const auto digest = radar_qt_example::verify_rd_frame(
                frame.metadata(), data.values(), data.rows(), data.columns());
            if (received == 1 || received % options.log_every == 0) {
                std::cout
                    << "[PASSED] RDMap Frame Verification Success! sha256="
                    << digest << " received=" << received << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[rd-sink] FAIL " << error.what() << '\n';
        return 1;
    }
}
