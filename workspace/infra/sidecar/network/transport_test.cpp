#include "ucx_transport.hpp"

#include <array>
#include <csignal>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

namespace {

using sidecar::network::EndpointOptions;
using sidecar::network::UCXMemoryRegion;
using sidecar::network::UCXRequest;
using sidecar::network::UCXTransport;

constexpr std::uint64_t kTestTag = 0x51;

int run_server(std::uint16_t port) {
    try {
        UCXTransport transport = UCXTransport::accept_one(
            EndpointOptions{"127.0.0.1", port, std::chrono::seconds{10}});
        std::array<std::byte, 64> input{};
        UCXMemoryRegion input_memory =
            transport.register_memory(input);
        UCXRequest receive = transport.receive(
            input, kTestTag, UINT64_MAX, &input_memory);
        transport.wait(receive);
        if (receive.bytes_transferred() != input.size()) {
            return 1;
        }
        UCXRequest send =
            transport.send(input, kTestTag, &input_memory);
        transport.wait(send);
        return 0;
    } catch (...) {
        return 1;
    }
}

int run_client(std::uint16_t port) {
    try {
        UCXTransport transport = UCXTransport::connect(
            EndpointOptions{"127.0.0.1", port, std::chrono::seconds{10}});
        std::array<std::byte, 64> output{};
        for (std::size_t index = 0; index < output.size(); ++index) {
            output[index] = static_cast<std::byte>(index);
        }
        UCXMemoryRegion output_memory =
            transport.register_memory(output);
        UCXRequest send =
            transport.send(output, kTestTag, &output_memory);
        transport.wait(send);
        std::array<std::byte, 64> reply{};
        UCXMemoryRegion reply_memory =
            transport.register_memory(reply);
        UCXRequest receive = transport.receive(
            reply, kTestTag, UINT64_MAX, &reply_memory);
        transport.wait(receive);
        if (reply != output ||
            receive.bytes_transferred() != reply.size()) {
            throw std::runtime_error("payload mismatch");
        }

        bool empty_rejected = false;
        try {
            std::span<const std::byte> empty;
            static_cast<void>(transport.send(empty, kTestTag));
        } catch (const std::invalid_argument&) {
            empty_rejected = true;
        }
        if (!empty_rejected) {
            throw std::runtime_error("empty send was not rejected");
        }

        bool outside_region_rejected = false;
        try {
            static_cast<void>(transport.send(
                reply, kTestTag, &output_memory));
        } catch (const std::invalid_argument&) {
            outside_region_rejected = true;
        }
        if (!outside_region_rejected) {
            throw std::runtime_error(
                "out-of-region send was not rejected");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ucx-transport-test: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

int main() {
    ::setenv("UCX_TLS", "tcp,self", 1);
    const std::uint16_t port =
        static_cast<std::uint16_t>(20'000 + (::getpid() % 20'000));
    for (int round = 0; round < 2; ++round) {
        const pid_t server_pid = ::fork();
        if (server_pid == -1) {
            std::cerr << "fork failed\n";
            return 1;
        }
        if (server_pid == 0) {
            ::_exit(run_server(port));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        const int client_status = run_client(port);
        if (client_status != 0) {
            ::kill(server_pid, SIGTERM);
        }
        int server_status = 0;
        if (::waitpid(server_pid, &server_status, 0) == -1 ||
            !WIFEXITED(server_status) ||
            WEXITSTATUS(server_status) != 0 || client_status != 0) {
            std::cerr << "ucx-transport-test: round " << round
                      << " failed\n";
            return 1;
        }
    }
    return 0;
}
