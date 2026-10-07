#pragma once

#include "forwarder/forwarder.hpp"

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sidecar::preview {

enum class Leg : std::uint8_t {
    input,
    output,
};

struct StreamConfig {
    bool enabled{false};
    std::uint64_t type_id{0};
    std::uint32_t type_version{0};
    std::size_t max_frame_bytes{0};
};

struct Config {
    std::string node_id;
    std::string instance_id;
    std::string host;
    std::uint16_t port{9901};
    StreamConfig input;
    StreamConfig output;
};

struct Counters {
    std::uint64_t captured{0};
    std::uint64_t snapshot_drops{0};
    std::uint64_t encode_drops{0};
    std::uint64_t network_drops{0};
};

class Runtime final {
public:
    explicit Runtime(Config config);
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    [[nodiscard]] forwarder::FrameTap* input_tap() noexcept;
    [[nodiscard]] forwarder::FrameTap* output_tap() noexcept;
    [[nodiscard]] Counters counters(Leg leg) const noexcept;

    void run(volatile std::sig_atomic_t& running) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Exposed for deterministic contract and cross-language golden tests.
[[nodiscard]] std::vector<std::uint8_t> encode_frame_for_test(
    std::span<const std::byte> frame,
    Leg leg,
    const std::string& node_id = "test-node",
    const std::string& instance_id = "test-instance");

}  // namespace sidecar::preview
