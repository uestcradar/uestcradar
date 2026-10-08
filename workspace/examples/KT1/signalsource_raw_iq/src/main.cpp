#include "raw_iq_fixture.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
std::uint64_t number(std::string_view text) {
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("invalid unsigned option value");
    return value;
}
}

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        std::uint64_t frames = 0, count = 8192, rate = 30720000;
        for (int n = 1; n < argc; ++n) {
            const std::string_view arg = argv[n];
            if (arg == "--help") {
                std::cout << "signalsource-raw-iq [--frames N] [--samples N] [--sample-rate N]\n"
                             "RawIQFrame 4:1, one channel, synthetic IQ/TX/RX. Rate 0 = unpaced.\n";
                return 0;
            }
            if (n + 1 == argc) throw std::invalid_argument("missing option value");
            const auto value = number(argv[++n]);
            if (arg == "--frames") frames = value;
            else if (arg == "--samples") count = value;
            else if (arg == "--sample-rate") rate = value;
            else throw std::invalid_argument("unknown option");
        }
        if (!count || count > 1048576 || rate > 1000000000)
            throw std::invalid_argument("samples must be 1..1048576; sample-rate must be 0..1000000000");
        struct sigaction action{};
        action.sa_handler = stop;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGTERM, &action, nullptr) || sigaction(SIGINT, &action, nullptr))
            throw std::runtime_error("cannot install stop handlers");
        uestcradar::Output<uestcradar::RawIQFrame> output(std::chrono::seconds(3));
        auto samples = raw_iq_fixture::samples(static_cast<std::uint32_t>(count));
        std::uint64_t sent = 0;
        const auto start = std::chrono::steady_clock::now();
        auto blocked_since = start;
        while (!stopped && (!frames || sent < frames)) {
            if (rate) {
                const auto due = start + std::chrono::duration<double>(double(sent) * count / rate);
                if (std::chrono::steady_clock::now() < due) {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                    blocked_since = std::chrono::steady_clock::now();
                    continue;
                }
            }
            const auto metadata = raw_iq_fixture::metadata(sent, static_cast<std::uint32_t>(count));
            auto frame = output.try_create(metadata);
            if (!frame) {
                if (std::chrono::steady_clock::now() - blocked_since > std::chrono::seconds(5))
                    throw std::runtime_error("output remained full for 5 seconds");
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                continue;
            }
            raw_iq_fixture::mark(samples, sent);
            std::copy(samples.begin(), samples.end(), frame->data().values().begin());
            output.write(std::move(*frame));
            ++sent;
            blocked_since = std::chrono::steady_clock::now();
        }
        std::cout << "[raw-iq] synthetic=true channel_count=1 frames=" << sent
                  << " samples_per_channel=" << count << '\n';
        return stopped && frames && sent != frames ? 130 : 0;
    } catch (const std::exception& error) {
        std::cerr << "[raw-iq] error=" << error.what() << '\n';
        return 1;
    }
}
