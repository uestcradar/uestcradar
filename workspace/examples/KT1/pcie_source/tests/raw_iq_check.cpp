// Offline acceptance utility. Uses the SDK itself to decode the new contract;
// no second copy of Metadata wire offsets or production recorder implementation.
#include "raw_iq.hpp"
#include "ringbuf/ringbuf.hpp"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {
constexpr std::uint32_t max_payload = 24 + 4 * 1048576;
struct Ring {
    std::string name;
    RingBuffer* value;
    explicit Ring(std::string n) : name(std::move(n)), value(ringbuf_create(name.c_str(), {2, max_payload, 4, 1})) {}
    ~Ring() { ringbuf_close(value); ringbuf_unlink(name.c_str()); }
};
void read(std::istream& input, void* data, std::size_t size) {
    if (!input.read(static_cast<char*>(data), static_cast<std::streamsize>(size)))
        throw std::runtime_error("truncated capture");
}
std::uint64_t integer(std::string_view text) {
    std::uint64_t value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) throw std::invalid_argument("invalid expected count/digest");
    return value;
}
void verify_fixture(std::uint64_t index, const uestcradar::RawIQMetadata& metadata,
                    std::span<const uestcradar::ComplexInt16> data) {
    const auto tx = index == 0 ? UINT64_MAX : UINT64_C(0xfedcba9800000000) + index * 32;
    const auto rx = UINT64_C(0x8000000000000000) + index * 98304;
    if (metadata.tx_timestamp != tx || metadata.rx_timestamp != rx)
        throw std::runtime_error("fixture timestamps differ");
    std::uint32_t state = 0x12345678;
    for (std::size_t n = 0; n < data.size(); ++n) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        const auto word = n == 0 ? static_cast<std::uint32_t>(index) : state;
        if (static_cast<std::uint16_t>(data[n].i) != (word & 65535) ||
            static_cast<std::uint16_t>(data[n].q) != (word >> 16))
            throw std::runtime_error("fixture IQ bytes differ");
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--hold-ring") {
            Ring ring(argv[2]);
            std::cout << "ready" << std::endl;
            std::string command;
            while (std::cin >> command && command == "used")
                std::cout << ringbuf_occupied_slots(ring.value) << std::endl;
            return 0;
        }
        if (argc < 2) throw std::invalid_argument("raw-iq-check FILE [--fixture] [--frames N] [--digest FNV1A64]");
        bool fixture = false;
        std::optional<std::uint64_t> expected_frames, expected_digest;
        for (int n = 2; n < argc; ++n) {
            const std::string_view arg = argv[n];
            if (arg == "--fixture") fixture = true;
            else if (n + 1 < argc && arg == "--frames") expected_frames = integer(argv[++n]);
            else if (n + 1 < argc && arg == "--digest") expected_digest = integer(argv[++n]);
            else throw std::invalid_argument("unknown/incomplete checker option");
        }
        Ring ring("/rawiq_check_" + std::to_string(getpid()));
        setenv("UESTCRADAR_UPSTREAM_SHM_NAME", ring.name.c_str(), 1);
        uestcradar::Input<uestcradar::RawIQFrame> decoder;
        std::ifstream file(argv[1], std::ios::binary);
        char magic[8];
        read(file, magic, 8);
        if (std::memcmp(magic, "USINK001", 8)) throw std::runtime_error("invalid capture magic");
        raw_iq::Digest digest;
        std::uint64_t raw_bytes = 0;
        for (;;) {
            std::uint64_t length;
            read(file, &length, sizeof(length));
            if (length == 0) {
                std::uint64_t counts[2];
                read(file, counts, sizeof(counts));
                if (counts[0] != digest.frames || counts[1] != raw_bytes || file.peek() != EOF)
                    throw std::runtime_error("invalid footer or trailing bytes");
                break;
            }
            if (length < sizeof(uestcradar::Envelope) + 24 || length > sizeof(uestcradar::Envelope) + max_payload)
                throw std::runtime_error("invalid or oversized raw IQ record");
            RingWriteLease lease;
            if (ringbuf_reserve(ring.value, lease) != RingResult::ok) throw std::runtime_error("checker Ring reserve failed");
            read(file, &lease.envelope(), sizeof(uestcradar::Envelope));
            const auto& e = lease.envelope();
            if (e.type_id != 4 || e.type_version != 1 || e.payload_length != length - sizeof(e) || e.frame_id != digest.frames + 1)
                throw std::runtime_error("capture contract/length/sequence differs");
            read(file, lease.payload().data(), e.payload_length);
            if (ringbuf_commit(lease) != RingResult::ok) throw std::runtime_error("checker Ring commit failed");
            {
                const auto frame = decoder.read();
                const auto metadata = frame.metadata();
                const auto data = frame.data().values();
                if (metadata.channel_count != 1) throw std::runtime_error("capture is not single-channel");
                if (fixture) verify_fixture(digest.frames, metadata, data);
                digest.add(metadata, data);
            }
            raw_bytes += length;
        }
        if (!digest.frames || (expected_frames && *expected_frames != digest.frames) ||
            (expected_digest && *expected_digest != digest.value))
            throw std::runtime_error("capture frame count/business digest differs");
        std::cout << "{\"complete\":true,\"frames\":" << digest.frames << ",\"samples\":" << digest.samples
                  << ",\"raw_bytes\":" << raw_bytes << ",\"business_fnv1a64\":\"" << digest.value
                  << "\",\"sample_continuity\":\"unverified\"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "raw-iq-check: " << error.what() << '\n';
        return 1;
    }
}
