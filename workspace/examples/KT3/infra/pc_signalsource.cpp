#include <data.h>

#include "pc_generator.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

struct Options {
    std::uint64_t cpis{0};
    std::uint64_t seed{};
    bool seed_set{false};
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
        if (argument == "--cpis" && index + 1 < argc) {
            options.cpis = parse_uint64(argv[++index]);
        } else if (argument == "--seed" && index + 1 < argc) {
            options.seed = parse_uint64(argv[++index]);
            options.seed_set = true;
        } else {
            throw std::invalid_argument("unknown or incomplete option");
        }
    }
    return options;
}

std::uint64_t random_seed() {
    std::random_device device;
    return (static_cast<std::uint64_t>(device()) << 32U) ^
        static_cast<std::uint64_t>(device()) ^
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const auto options = parse_options(argc, argv);
        const auto seed = options.seed_set ? options.seed : random_seed();
        radar_qt_example::RandomPulseGenerator generator(seed);
        uestcradar::Output<uestcradar::PulseCompressionFrame> output;

        std::cout << "[pc-signalsource] ready seed=" << seed
                  << " shape=1x" << radar_qt_example::kRangeBinCount
                  << " pulses_per_cpi="
                  << radar_qt_example::kPulsesPerCpi << '\n';

        for (std::uint64_t cpi = 0;
             options.cpis == 0 || cpi < options.cpis;
             ++cpi) {
            for (std::uint32_t pulse = 0;
                 pulse < radar_qt_example::kPulsesPerCpi;
                 ++pulse) {
                auto frame = output.create(
                    radar_qt_example::describe_pulse(pulse));
                generator.fill(frame.data().values());
                output.write(std::move(frame));
            }
            std::cout << "[pc-signalsource] emitted_cpi=" << cpi
                      << " frames=" << radar_qt_example::kPulsesPerCpi
                      << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[pc-signalsource] FAIL " << error.what() << '\n';
        return 1;
    }
}
