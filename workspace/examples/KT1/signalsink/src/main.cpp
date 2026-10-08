#include "recorder.hpp"
#include <sdk.h>

#include <atomic>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace signalsink {
int control_client(int argc, char** argv);
Fd control_listener(Fd& lock);
void control_server(int listener, Recorder& recorder, const std::atomic<bool>& stopping);
}

namespace {
volatile std::sig_atomic_t interrupted = 0;
void on_signal(int) { interrupted = 1; }
std::uint64_t number(const std::string& text) {
    std::uint64_t value{};
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !value)
        throw std::invalid_argument("expected positive integer");
    return value;
}
std::string environment(const char* name, const char* fallback) {
    const auto* value = std::getenv(name);
    return value && *value ? value : fallback;
}
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "control") return signalsink::control_client(argc, argv);
        std::string root = environment("SIGNALSINK_CAPTURE_ROOT", "/captures");
        auto queue = number(environment("SIGNALSINK_QUEUE_BYTES", "536870912"));
        auto reserve = number(environment("SIGNALSINK_MIN_FREE_BYTES", "1073741824"));
        std::string contract = environment("SIGNALSINK_INPUT", "");
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 >= argc) throw std::invalid_argument("expected option value");
            const std::string option = argv[i];
            if (option == "--capture-root") root = argv[i + 1];
            else if (option == "--queue-bytes") queue = number(argv[i + 1]);
            else if (option == "--min-free-bytes") reserve = number(argv[i + 1]);
            else if (option == "--input") contract = argv[i + 1];
            else throw std::invalid_argument("unknown SignalSink option");
        }
        const auto separator = contract.find(':');
        if (separator == std::string::npos) throw std::invalid_argument("SIGNALSINK_INPUT or --input must bind a concrete TYPE:VERSION");
        const auto type = number(contract.substr(0, separator)), version = number(contract.substr(separator + 1));
        if (version > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("type version exceeds uint32");
        uestcradar::Input<uestcradar::RawFrame> input(type, static_cast<std::uint32_t>(version));
        signalsink::Fd lock;
        auto listener = signalsink::control_listener(lock);
        signalsink::Recorder recorder(root, queue, reserve);
        std::signal(SIGTERM, on_signal);
        std::signal(SIGINT, on_signal);
        std::atomic<bool> stopping{false};
        std::thread controls([&] { signalsink::control_server(listener.get(), recorder, stopping); });
        int result = 0;
        try {
            while (!interrupted) {
                if (auto frame = input.try_read()) recorder.accept(frame->bytes());
                else std::this_thread::sleep_for(std::chrono::microseconds{50});
            }
        } catch (const std::exception& error) {
            recorder.fail(error.what());
            std::cerr << "SignalSink input failed: " << error.what() << '\n';
            result = 1;
        }
        stopping.store(true);
        controls.join();
        return result;
    } catch (const std::exception& error) {
        std::cerr << "SignalSink: " << error.what() << '\n';
        return 1;
    }
}
