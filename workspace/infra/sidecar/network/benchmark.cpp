#include "ucx_transport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using sidecar::network::EndpointOptions;
using sidecar::network::UCXRequest;
using sidecar::network::UCXTransport;

constexpr std::uint64_t kCorrectnessTag = 0x100;
constexpr std::uint64_t kWarmupTag = 0x101;
constexpr std::uint64_t kStreamingTag = 0x102;
constexpr std::uint64_t kAckTag = 0x103;
constexpr std::uint64_t kPingPongTag = 0x104;
constexpr std::array<std::size_t, 3> kBlockSizes{
    4 * 1024,
    64 * 1024,
    512 * 1024,
};
constexpr std::size_t kWindow = 64;
constexpr std::size_t kWarmupIterations = 16;

struct Arguments {
    bool server{false};
    bool quick{false};
    std::string address;
    std::uint16_t port{13337};
};

std::uint16_t parse_port(std::string_view value) {
    char* end = nullptr;
    const std::string text{value};
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || parsed == 0 || parsed > 65535) {
        throw std::runtime_error("port must be in range 1..65535");
    }
    return static_cast<std::uint16_t>(parsed);
}

Arguments parse_arguments(int argc, char* argv[]) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--server") {
            result.server = true;
        } else if (argument == "--client") {
            result.server = false;
        } else if (argument == "--quick") {
            result.quick = true;
        } else if ((argument == "--bind" || argument == "--host") &&
                   index + 1 < argc) {
            result.address = argv[++index];
        } else if (argument == "--port" && index + 1 < argc) {
            result.port = parse_port(argv[++index]);
        } else {
            throw std::runtime_error(
                "usage: ucx-network-benchmark "
                "(--server --bind ADDRESS | --client --host ADDRESS) "
                "[--port PORT] [--quick]");
        }
    }
    if (result.address.empty()) {
        result.address = result.server ? "0.0.0.0" : "127.0.0.1";
    }
    return result;
}

std::vector<std::byte> patterned_buffer(std::size_t size) {
    std::vector<std::byte> buffer(size);
    for (std::size_t index = 0; index < size; ++index) {
        buffer[index] = static_cast<std::byte>((index * 131 + 17) & 0xff);
    }
    return buffer;
}

std::size_t streaming_iterations(std::size_t block_size, bool quick) {
    const std::size_t target_bytes =
        quick ? 8 * 1024 * 1024 : 512 * 1024 * 1024;
    return std::max<std::size_t>(kWindow, target_bytes / block_size);
}

std::size_t ping_pong_iterations(std::size_t block_size, bool quick) {
    const std::size_t target_bytes =
        quick ? 2 * 1024 * 1024 : 64 * 1024 * 1024;
    const std::size_t maximum = quick ? 200 : 5'000;
    return std::clamp<std::size_t>(target_bytes / block_size, 20, maximum);
}

void wait_all(UCXTransport& transport, std::vector<UCXRequest>& requests) {
    for (UCXRequest& request : requests) {
        transport.wait(request);
    }
    requests.clear();
}

void send_ack(UCXTransport& transport) {
    const std::byte ack{0x42};
    UCXRequest request =
        transport.send(std::span<const std::byte>{&ack, 1}, kAckTag);
    transport.wait(request);
}

void receive_ack(UCXTransport& transport) {
    std::byte ack{};
    UCXRequest request =
        transport.receive(std::span<std::byte>{&ack, 1}, kAckTag);
    transport.wait(request);
    if (ack != std::byte{0x42}) {
        throw std::runtime_error("invalid benchmark acknowledgement");
    }
}

void run_server_case(
    UCXTransport& transport,
    std::size_t block_size,
    bool quick) {
    std::vector<std::byte> correctness(block_size);
    UCXRequest correctness_request = transport.receive(
        correctness,
        kCorrectnessTag);
    transport.wait(correctness_request);
    if (correctness_request.bytes_transferred() != block_size ||
        correctness != patterned_buffer(block_size)) {
        throw std::runtime_error("benchmark data integrity failure");
    }
    send_ack(transport);

    std::vector<std::byte> warmup_buffer(block_size);
    for (std::size_t index = 0; index < kWarmupIterations; ++index) {
        UCXRequest receive = transport.receive(warmup_buffer, kWarmupTag);
        transport.wait(receive);
        UCXRequest send = transport.send(warmup_buffer, kWarmupTag);
        transport.wait(send);
    }

    const std::size_t iterations =
        streaming_iterations(block_size, quick);
    std::vector<std::byte> receive_storage(kWindow * block_size);
    std::vector<UCXRequest> requests;
    requests.reserve(kWindow);
    for (std::size_t completed = 0; completed < iterations;) {
        const std::size_t batch =
            std::min(kWindow, iterations - completed);
        for (std::size_t index = 0; index < batch; ++index) {
            std::span<std::byte> slot{
                receive_storage.data() + index * block_size,
                block_size,
            };
            requests.push_back(transport.receive(slot, kStreamingTag));
        }
        wait_all(transport, requests);
        completed += batch;
    }
    send_ack(transport);

    std::vector<std::byte> ping_buffer(block_size);
    const std::size_t ping_iterations =
        ping_pong_iterations(block_size, quick);
    for (std::size_t index = 0; index < ping_iterations; ++index) {
        UCXRequest receive =
            transport.receive(ping_buffer, kPingPongTag);
        transport.wait(receive);
        UCXRequest send =
            transport.send(ping_buffer, kPingPongTag);
        transport.wait(send);
    }
}

struct Latency {
    double mean_us;
    double p50_us;
    double p99_us;
};

Latency summarize_latency(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    double total = 0;
    for (double sample : samples) {
        total += sample;
    }
    const auto percentile = [&samples](double value) {
        const std::size_t index = static_cast<std::size_t>(
            std::ceil(value * static_cast<double>(samples.size()))) - 1;
        return samples[std::min(index, samples.size() - 1)];
    };
    return Latency{
        total / static_cast<double>(samples.size()),
        percentile(0.50),
        percentile(0.99),
    };
}

void print_header() {
    std::cout
        << std::left
        << std::setw(12) << "Block"
        << std::right
        << std::setw(14) << "GiB/s"
        << std::setw(16) << "OPS"
        << std::setw(16) << "RTT mean us"
        << std::setw(14) << "RTT p50"
        << std::setw(14) << "RTT p99"
        << '\n';
}

void run_client_case(
    UCXTransport& transport,
    std::size_t block_size,
    bool quick) {
    std::vector<std::byte> buffer = patterned_buffer(block_size);
    UCXRequest correctness =
        transport.send(buffer, kCorrectnessTag);
    transport.wait(correctness);
    receive_ack(transport);

    std::vector<std::byte> warmup_reply(block_size);
    for (std::size_t index = 0; index < kWarmupIterations; ++index) {
        UCXRequest send = transport.send(buffer, kWarmupTag);
        transport.wait(send);
        UCXRequest receive = transport.receive(warmup_reply, kWarmupTag);
        transport.wait(receive);
    }

    const std::size_t iterations =
        streaming_iterations(block_size, quick);
    std::vector<UCXRequest> requests;
    requests.reserve(kWindow);
    const auto streaming_start = std::chrono::steady_clock::now();
    for (std::size_t completed = 0; completed < iterations;) {
        const std::size_t batch =
            std::min(kWindow, iterations - completed);
        for (std::size_t index = 0; index < batch; ++index) {
            requests.push_back(transport.send(buffer, kStreamingTag));
        }
        wait_all(transport, requests);
        completed += batch;
    }
    receive_ack(transport);
    const double streaming_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - streaming_start).count();
    const double bytes =
        static_cast<double>(iterations) * static_cast<double>(block_size);

    const std::size_t ping_iterations =
        ping_pong_iterations(block_size, quick);
    std::vector<std::byte> reply(block_size);
    std::vector<double> samples;
    samples.reserve(ping_iterations);
    for (std::size_t index = 0; index < ping_iterations; ++index) {
        const auto started_at = std::chrono::steady_clock::now();
        UCXRequest send = transport.send(buffer, kPingPongTag);
        transport.wait(send);
        UCXRequest receive =
            transport.receive(reply, kPingPongTag);
        transport.wait(receive);
        samples.push_back(std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - started_at).count());
    }
    const Latency latency = summarize_latency(std::move(samples));

    const std::string block_label =
        block_size >= 1024
            ? std::to_string(block_size / 1024) + " KiB"
            : std::to_string(block_size) + " B";
    std::cout
        << std::left << std::setw(12) << block_label
        << std::right << std::fixed << std::setprecision(2)
        << std::setw(14) << bytes / streaming_seconds /
                              (1024.0 * 1024.0 * 1024.0)
        << std::setw(16) << static_cast<double>(iterations) /
                              streaming_seconds
        << std::setw(16) << latency.mean_us
        << std::setw(14) << latency.p50_us
        << std::setw(14) << latency.p99_us
        << '\n';
}

UCXTransport connect_with_retry(const Arguments& arguments) {
    EndpointOptions options{
        arguments.address,
        arguments.port,
        std::chrono::seconds{2},
    };
    std::string last_error;
    for (int attempt = 0; attempt < 15; ++attempt) {
        try {
            return UCXTransport::connect(options);
        } catch (const std::exception& error) {
            last_error = error.what();
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }
    }
    throw std::runtime_error("connect failed: " + last_error);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        if (arguments.server) {
            UCXTransport transport = UCXTransport::accept_one(
                EndpointOptions{
                    arguments.address,
                    arguments.port,
                    std::chrono::seconds{30},
                });
            for (std::size_t block_size : kBlockSizes) {
                run_server_case(transport, block_size, arguments.quick);
            }
            return 0;
        }

        UCXTransport transport = connect_with_retry(arguments);
        print_header();
        for (std::size_t block_size : kBlockSizes) {
            run_client_case(transport, block_size, arguments.quick);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ucx-network-benchmark: " << error.what() << '\n';
        return 1;
    }
}
