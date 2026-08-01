#include "forwarder/forwarder_protocol.hpp"
#include "network/ucx_transport.hpp"
#include "raw_frame.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using sidecar::forwarder::protocol::CreditBytes;
using sidecar::network::DataPathMode;
using sidecar::network::EndpointOptions;
using sidecar::network::UCXMemoryRegion;
using sidecar::network::UCXRequest;
using sidecar::network::UCXTransport;
namespace protocol = sidecar::forwarder::protocol;

struct Arguments {
    std::string host{"127.0.0.1"};
    std::string role{"consumer"};
    std::uint16_t port{13337};
    std::chrono::seconds duration{24};
    std::size_t chunk_bytes{256 * 1024};
    std::string profile{"jitter"};
    double produce_mib_s{64.0};
    double consume_mib_s{64.0};
    DataPathMode data_path{DataPathMode::functional};
};

std::uint64_t parse_unsigned(
    std::string_view text,
    const char* name) {
    const std::string value{text};
    char* end = nullptr;
    const unsigned long long parsed =
        std::strtoull(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        throw std::invalid_argument(std::string{name} + " is invalid");
    }
    return parsed;
}

double parse_rate(std::string_view text, const char* name) {
    const std::string value{text};
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' ||
        !std::isfinite(parsed) || parsed < 0.0) {
        throw std::invalid_argument(std::string{name} + " is invalid");
    }
    return parsed;
}

Arguments parse_arguments(int argc, char* argv[]) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto next = [&]() -> std::string_view {
            if (++index >= argc) {
                throw std::invalid_argument(
                    std::string{argument} + " requires a value");
            }
            return argv[index];
        };

        if (argument == "--host") {
            result.host = next();
        } else if (argument == "--role") {
            result.role = next();
            if (result.role != "producer" &&
                result.role != "consumer") {
                throw std::invalid_argument(
                    "role must be producer or consumer");
            }
        } else if (argument == "--port") {
            const std::uint64_t port = parse_unsigned(next(), "port");
            if (port == 0 || port > 65535) {
                throw std::invalid_argument("port is out of range");
            }
            result.port = static_cast<std::uint16_t>(port);
        } else if (argument == "--duration") {
            const std::uint64_t seconds =
                parse_unsigned(next(), "duration");
            if (seconds == 0) {
                throw std::invalid_argument(
                    "duration must be positive");
            }
            result.duration = std::chrono::seconds{seconds};
        } else if (argument == "--chunk-bytes") {
            const std::uint64_t bytes =
                parse_unsigned(next(), "chunk-bytes");
            if (bytes == 0 ||
                bytes > static_cast<std::uint64_t>(
                            std::numeric_limits<std::size_t>::max())) {
                throw std::invalid_argument(
                    "chunk-bytes is out of range");
            }
            result.chunk_bytes = static_cast<std::size_t>(bytes);
        } else if (argument == "--profile") {
            result.profile = next();
            if (result.profile != "steady" &&
                result.profile != "jitter") {
                throw std::invalid_argument(
                    "profile must be steady or jitter");
            }
        } else if (argument == "--produce-mib-s") {
            result.produce_mib_s =
                parse_rate(next(), "produce-mib-s");
        } else if (argument == "--consume-mib-s") {
            result.consume_mib_s =
                parse_rate(next(), "consume-mib-s");
        } else if (argument == "--strict-rdma") {
            result.data_path = DataPathMode::strict_rdma;
        } else {
            throw std::invalid_argument(
                "usage: network-benchmark [--host HOST] [--port PORT] "
                "[--role producer|consumer] "
                "[--duration SEC] [--chunk-bytes BYTES] "
                "[--profile steady|jitter] "
                "[--produce-mib-s RATE] [--consume-mib-s RATE] "
                "[--strict-rdma]");
        }
    }
    return result;
}

struct Rates {
    double produce;
    double consume;
    const char* phase;
};

Rates current_rates(
    const Arguments& arguments,
    Clock::time_point started) {
    if (arguments.profile == "steady") {
        return {
            arguments.produce_mib_s,
            arguments.consume_mib_s,
            "steady",
        };
    }

    const auto phase = static_cast<unsigned>(
        std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - started).count() /
        3) %
        4;
    switch (phase) {
        case 0:
            return {64.0, 64.0, "steady"};
        case 1:
            return {512.0, 32.0, "producer-burst"};
        case 2:
            return {64.0, 512.0, "consumer-drain"};
        default:
            return {8.0, 64.0, "quiet"};
    }
}

std::chrono::nanoseconds pacing_delay(
    std::size_t bytes,
    double mib_per_second) {
    if (mib_per_second <= 0.0) {
        return std::chrono::hours{24};
    }
    const double seconds =
        static_cast<double>(bytes) /
        (mib_per_second * 1024.0 * 1024.0);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>{seconds});
}

UCXTransport connect_with_retry(const Arguments& arguments) {
    std::string last_error;
    for (int attempt = 0; attempt < 30; ++attempt) {
        try {
            return UCXTransport::connect(EndpointOptions{
                arguments.host,
                arguments.port,
                std::chrono::seconds{2},
                arguments.data_path,
            });
        } catch (const std::exception& error) {
            last_error = error.what();
            std::this_thread::sleep_for(
                std::chrono::milliseconds{250});
        }
    }
    throw std::runtime_error("connect failed: " + last_error);
}

void exchange_and_validate_contract(
    UCXTransport& transport,
    std::size_t chunk_bytes,
    bool producer) {
    constexpr std::uint64_t iq_frame_type_id = 1;
    if (chunk_bytes > UINT32_MAX) {
        throw std::invalid_argument(
            "chunk-bytes exceeds forwarder protocol limit");
    }
    const protocol::PortContract port{
        iq_frame_type_id,
        2,
        static_cast<std::uint32_t>(chunk_bytes),
    };
    protocol::HelloBytes incoming{};
    protocol::HelloBytes outgoing =
        protocol::encode_hello({
            producer
                ? protocol::PortRole::producer
                : protocol::PortRole::consumer,
            port,
        });
    UCXRequest receive =
        transport.receive(incoming, protocol::kHelloTag);
    UCXRequest send =
        transport.send(outgoing, protocol::kHelloTag);
    transport.wait(send);
    transport.wait(receive);
    protocol::Hello remote{};
    if (receive.bytes_transferred() != incoming.size() ||
        !protocol::decode_hello(incoming, remote) ||
        remote.role == (producer
                           ? protocol::PortRole::producer
                           : protocol::PortRole::consumer) ||
        remote.contract.type_id != port.type_id ||
        remote.contract.type_version != port.type_version ||
        (producer
             ? port.max_payload_bytes >
                   remote.contract.max_payload_bytes
             : remote.contract.max_payload_bytes >
                   port.max_payload_bytes)) {
        throw std::runtime_error(
            "sidecar forwarder contract is incompatible");
    }
}

class BenchmarkPeer {
public:
    BenchmarkPeer(
        UCXTransport& transport,
        std::size_t chunk_bytes)
        : transport_(transport),
          chunk_bytes_(chunk_bytes),
          send_buffer_(uestcradar::kEnvelopeSize + chunk_bytes, std::byte{0}),
          receive_buffer_(
              uestcradar::kEnvelopeSize + chunk_bytes, std::byte{0}),
          send_memory_(transport.register_memory(send_buffer_)),
          receive_memory_(transport.register_memory(receive_buffer_)) {}

    bool progress(
        const Rates& rates,
        Clock::time_point now,
        bool producer) {
        return producer
                   ? progress_outbound(rates.produce, now)
                   : progress_inbound(rates.consume, now);
    }

    std::uint64_t take_sent_bytes() noexcept {
        return std::exchange(interval_sent_, 0);
    }

    std::uint64_t take_received_bytes() noexcept {
        return std::exchange(interval_received_, 0);
    }

    std::uint64_t total_sent() const noexcept {
        return total_sent_;
    }

    std::uint64_t total_received() const noexcept {
        return total_received_;
    }

private:
    bool progress_outbound(
        double rate,
        Clock::time_point now) {
        bool activity = false;
        if (!credit_receive_active_ && !credit_available_) {
            credit_receive_ = transport_.receive(
                outbound_credit_,
                protocol::kCreditTag);
            credit_receive_active_ = true;
            activity = true;
        }
        if (credit_receive_active_ && credit_receive_.completed()) {
            transport_.wait(credit_receive_);
            if (credit_receive_.bytes_transferred() !=
                outbound_credit_.size()) {
                throw std::runtime_error("invalid peer credit");
            }
            const std::uint64_t decoded =
                protocol::decode_credit(outbound_credit_);
            if (decoded == 0 ||
                decoded > std::numeric_limits<std::size_t>::max()) {
                throw std::runtime_error("invalid peer credit value");
            }
            peer_credit_ = static_cast<std::size_t>(decoded);
            credit_receive_active_ = false;
            credit_available_ = true;
            activity = true;
        }
        if (credit_available_ && !payload_send_active_ &&
            now >= next_send_ && rate > 0.0) {
            if (peer_credit_ <= uestcradar::kEnvelopeSize) {
                throw std::runtime_error("peer credit cannot hold Envelope");
            }
            payload_length_ = std::min(
                chunk_bytes_, peer_credit_ - uestcradar::kEnvelopeSize);
            const uestcradar::Envelope envelope{
                .frame_id = ++sequence_,
                .type_id = 1,
                .type_version = 2,
                .payload_length =
                    static_cast<std::uint32_t>(payload_length_),
            };
            std::memcpy(
                send_buffer_.data(), &envelope, sizeof(envelope));
            frame_length_ = uestcradar::kEnvelopeSize + payload_length_;
            payload_send_ = transport_.send(
                std::span<const std::byte>{
                    send_buffer_.data(), frame_length_},
                protocol::kPayloadTag,
                &send_memory_);
            payload_send_active_ = true;
            activity = true;
        }
        if (payload_send_active_ && payload_send_.completed()) {
            transport_.wait(payload_send_);
            interval_sent_ += payload_length_;
            total_sent_ += payload_length_;
            next_send_ = std::max(next_send_, now) +
                         pacing_delay(payload_length_, rate);
            payload_send_active_ = false;
            credit_available_ = false;
            peer_credit_ = 0;
            payload_length_ = 0;
            frame_length_ = 0;
            activity = true;
        }
        return activity;
    }

    bool progress_inbound(
        double rate,
        Clock::time_point now) {
        bool activity = false;
        if (!receive_active_ && now >= next_receive_ && rate > 0.0) {
            payload_receive_ = transport_.receive(
                receive_buffer_,
                protocol::kPayloadTag,
                UINT64_MAX,
                &receive_memory_);
            inbound_credit_ =
                protocol::encode_credit(receive_buffer_.size());
            credit_send_ = transport_.send(
                inbound_credit_,
                protocol::kCreditTag);
            receive_active_ = true;
            activity = true;
        }
        if (receive_active_ && !payload_completed_ &&
            payload_receive_.completed()) {
            transport_.wait(payload_receive_);
            const std::size_t received =
                payload_receive_.bytes_transferred();
            uestcradar::Envelope envelope{};
            if (received >= sizeof(envelope)) {
                std::memcpy(&envelope, receive_buffer_.data(), sizeof(envelope));
            }
            const bool valid =
                received >= sizeof(envelope) &&
                envelope.type_id == 1 &&
                envelope.type_version == 2 &&
                envelope.payload_length <= chunk_bytes_ &&
                received == sizeof(envelope) + envelope.payload_length;
            if (!valid ||
                !std::all_of(
                    receive_buffer_.begin() +
                        static_cast<std::ptrdiff_t>(sizeof(envelope)),
                    receive_buffer_.begin() +
                        static_cast<std::ptrdiff_t>(received),
                    [](std::byte value) {
                        return value == std::byte{0};
                    })) {
                throw std::runtime_error(
                    "received payload failed validation");
            }
            interval_received_ += envelope.payload_length;
            total_received_ += envelope.payload_length;
            next_receive_ = std::max(next_receive_, now) +
                            pacing_delay(envelope.payload_length, rate);
            payload_completed_ = true;
            activity = true;
        }
        if (receive_active_ && !credit_completed_ &&
            credit_send_.completed()) {
            transport_.wait(credit_send_);
            credit_completed_ = true;
            activity = true;
        }
        if (receive_active_ && payload_completed_ &&
            credit_completed_) {
            receive_active_ = false;
            payload_completed_ = false;
            credit_completed_ = false;
            activity = true;
        }
        return activity;
    }

    UCXTransport& transport_;
    std::size_t chunk_bytes_;
    std::vector<std::byte> send_buffer_;
    std::vector<std::byte> receive_buffer_;
    UCXMemoryRegion send_memory_;
    UCXMemoryRegion receive_memory_;

    CreditBytes outbound_credit_{};
    CreditBytes inbound_credit_{};
    UCXRequest credit_receive_;
    UCXRequest credit_send_;
    UCXRequest payload_send_;
    UCXRequest payload_receive_;

    Clock::time_point next_send_{Clock::now()};
    Clock::time_point next_receive_{Clock::now()};
    std::size_t peer_credit_{0};
    std::size_t payload_length_{0};
    std::size_t frame_length_{0};
    std::uint64_t sequence_{0};
    bool credit_receive_active_{false};
    bool credit_available_{false};
    bool payload_send_active_{false};
    bool receive_active_{false};
    bool payload_completed_{false};
    bool credit_completed_{false};

    std::uint64_t interval_sent_{0};
    std::uint64_t interval_received_{0};
    std::uint64_t total_sent_{0};
    std::uint64_t total_received_{0};
};

double to_mib(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        const bool producer = arguments.role == "producer";
        UCXTransport transport = connect_with_retry(arguments);
        exchange_and_validate_contract(
            transport, arguments.chunk_bytes, producer);
        BenchmarkPeer peer{transport, arguments.chunk_bytes};

        const auto started = Clock::now();
        auto last_report = started;
        const auto deadline = started + arguments.duration;
        std::size_t idle = 0;

        std::cout
            << "network-benchmark: connected host=" << arguments.host
            << " port=" << arguments.port
            << " role=" << arguments.role
            << " profile=" << arguments.profile
            << " chunk=" << arguments.chunk_bytes << " bytes\n";

        while (Clock::now() < deadline) {
            const auto now = Clock::now();
            const Rates rates = current_rates(arguments, started);
            bool activity = transport.progress();
            activity = peer.progress(rates, now, producer) || activity;

            if (now - last_report >= std::chrono::seconds{1}) {
                const double elapsed =
                    std::chrono::duration<double>(
                        now - last_report).count();
                const double sent =
                    to_mib(peer.take_sent_bytes()) / elapsed;
                const double received =
                    to_mib(peer.take_received_bytes()) / elapsed;
                std::cout
                    << std::fixed << std::setprecision(2)
                    << "phase=" << rates.phase
                    << " target_tx=" << rates.produce
                    << " target_rx=" << rates.consume
                    << " tx_mib_s=" << sent
                    << " rx_mib_s=" << received
                    << '\n';
                last_report = now;
            }

            if (activity) {
                idle = 0;
            } else if (++idle < 64) {
                continue;
            } else if (idle < 128) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(
                    std::chrono::microseconds{50});
            }
        }

        std::cout
            << "network-benchmark: total_tx_mib="
            << std::fixed << std::setprecision(2)
            << to_mib(peer.total_sent())
            << " total_rx_mib="
            << to_mib(peer.total_received())
            << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "network-benchmark: " << error.what() << '\n';
        return 1;
    }
}
