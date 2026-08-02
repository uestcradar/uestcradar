#include <data.h>

#include "ringbuf/ringbuf.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <unistd.h>

namespace {

constexpr std::uint64_t kIQTypeId = 1;
constexpr std::uint64_t kPulseCompressionTypeId = 2;
constexpr std::uint64_t kRDTypeId = 3;
constexpr std::uint32_t kIQVersion = 2;
constexpr std::uint32_t kPulseCompressionVersion = 2;
constexpr std::uint32_t kRDVersion = 2;

class OwnedRing {
public:
    OwnedRing(
        std::string name,
        std::uint64_t type_id,
        std::uint32_t type_version,
        std::uint32_t slots = 2)
        : name_(std::move(name)),
          ring_(ringbuf_create(
              name_.c_str(), {slots, 4096, type_id, type_version})) {}

    ~OwnedRing() {
        ringbuf_shutdown(ring_);
        ringbuf_close(ring_);
        ringbuf_unlink(name_.c_str());
    }

    RingBuffer* get() const noexcept { return ring_; }
    const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
    RingBuffer* ring_;
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint32_t iq_payload_bytes(const uestcradar::IQMetadata& metadata) {
    return static_cast<std::uint32_t>(
        sizeof(metadata) + metadata.channel_count *
            metadata.samples_per_channel *
            sizeof(uestcradar::ComplexInt16));
}

void seed_iq(
    RingBuffer* ring,
    std::uint64_t frame_id,
    std::uint64_t timestamp = 123456) {
    const uestcradar::IQMetadata metadata{2, 3, 2.5e6, 1.2e9};
    RingWriteLease lease;
    require(
        ringbuf_reserve(ring, lease) == RingResult::ok,
        "could not reserve IQ frame");
    lease.envelope() = uestcradar::Envelope{
        .frame_id = frame_id,
        .timestamp = timestamp,
        .type_id = kIQTypeId,
        .type_version = kIQVersion,
        .payload_length = iq_payload_bytes(metadata),
    };
    std::memcpy(lease.payload().data(), &metadata, sizeof(metadata));
    auto* samples = reinterpret_cast<uestcradar::ComplexInt16*>(
        lease.payload().data() + sizeof(metadata));
    for (std::size_t index = 0; index < 6; ++index) {
        samples[index] = {
            static_cast<std::int16_t>(index + 1),
            static_cast<std::int16_t>(
                -static_cast<std::int32_t>(index + 1))};
    }
    require(
        ringbuf_commit(lease) == RingResult::ok,
        "could not commit IQ frame");
}

void test_typed_input_and_lifetime(const std::string& prefix) {
    OwnedRing upstream{prefix + "_iq_up", kIQTypeId, kIQVersion};
    ::setenv(
        "UESTCRADAR_UPSTREAM_SHM_NAME", upstream.name().c_str(), 1);
    seed_iq(upstream.get(), 42);
    seed_iq(upstream.get(), 43);

    uestcradar::Input<uestcradar::IQFrame> input;
    {
        auto iq = input.read();
        const auto metadata = iq.metadata();
        require(
            metadata.channel_count == 2 &&
                metadata.samples_per_channel == 3 &&
                iq.data().rows() == 2 &&
                iq.data().columns() == 3 &&
                iq.data()[1][2].i == 6,
            "typed IQ mapping is incorrect");
        iq.data()[0][0] = {321, -123};
        const auto* shared_samples =
            reinterpret_cast<const uestcradar::ComplexInt16*>(
                upstream.get()->slots + sizeof(uestcradar::Envelope) +
                sizeof(uestcradar::IQMetadata));
        require(
            shared_samples[0].i == 321 && shared_samples[0].q == -123,
            "typed IQ data was copied instead of viewing the RingBuffer Slot");

        RingWriteLease blocked;
        require(
            ringbuf_reserve(upstream.get(), blocked) ==
                RingResult::would_block,
            "input Slot was reused while IQFrame was alive");
    }

    RingWriteLease reusable;
    require(
        ringbuf_reserve(upstream.get(), reusable) == RingResult::ok,
        "input Slot was not released by IQFrame destructor");
    ringbuf_cancel(reusable);
}

void test_operator_inherits_trace(const std::string& prefix) {
    OwnedRing upstream{prefix + "_parent_up", kIQTypeId, kIQVersion};
    OwnedRing downstream{
        prefix + "_pulse_down", kPulseCompressionTypeId,
        kPulseCompressionVersion};
    ::setenv(
        "UESTCRADAR_UPSTREAM_SHM_NAME", upstream.name().c_str(), 1);
    ::setenv(
        "UESTCRADAR_DOWNSTREAM_SHM_NAME",
        downstream.name().c_str(),
        1);
    seed_iq(upstream.get(), 88, 987654321);

    uestcradar::Input<uestcradar::IQFrame> input;
    uestcradar::Output<uestcradar::PulseCompressionFrame> output;
    auto iq = input.read();
    const uestcradar::PulseCompressionMetadata metadata{
        .channel_count = 2,
        .range_bin_count = 3,
        .pulse_index = 7,
        .pulses_per_cpi = 128,
        .range_resolution_m = 1.5,
    };
    auto pulse = output.create(metadata, iq);
    std::fill(
        pulse.data().values().begin(),
        pulse.data().values().end(),
        uestcradar::ComplexFloat32{3.0F, 4.0F});
    output.write(std::move(pulse));

    RingReadLease committed;
    require(
        ringbuf_acquire(downstream.get(), committed) == RingResult::ok,
        "typed output was not committed");
    require(
        committed.envelope().frame_id == 88 &&
            committed.envelope().timestamp == 987654321 &&
            committed.envelope().type_id == kPulseCompressionTypeId &&
            committed.envelope().type_version == kPulseCompressionVersion &&
            committed.payload().size() ==
                sizeof(metadata) + 6 *
                    sizeof(uestcradar::ComplexFloat32),
            "SDK did not generate the expected hidden Envelope");
    std::uint32_t wire_pulse_index{};
    double wire_resolution{};
    std::memcpy(
        &wire_pulse_index, committed.payload().data() + 8,
        sizeof(wire_pulse_index));
    std::memcpy(
        &wire_resolution, committed.payload().data() + 16,
        sizeof(wire_resolution));
    require(
        wire_pulse_index == metadata.pulse_index &&
            wire_resolution == metadata.range_resolution_m,
        "generated pulse-compression wire metadata is incorrect");
    require(
        std::all_of(
            std::begin(committed.envelope().reserved),
            std::end(committed.envelope().reserved),
            [](std::byte value) { return value == std::byte{}; }),
        "SDK did not clear the Envelope reserved area");
    require(
        ringbuf_release(committed) == RingResult::ok,
        "could not release typed output");

    {
        auto abandoned = output.create(metadata, iq);
        abandoned.data()[0][0] = {7.0F, 8.0F};
    }
    RingWriteLease reusable;
    require(
        ringbuf_reserve(downstream.get(), reusable) == RingResult::ok,
        "abandoned typed frame did not cancel its Slot");
    ringbuf_cancel(reusable);
}

void test_source_generates_trace(const std::string& prefix) {
    OwnedRing downstream{prefix + "_rd_down", kRDTypeId, kRDVersion};
    ::setenv(
        "UESTCRADAR_DOWNSTREAM_SHM_NAME",
        downstream.name().c_str(),
        1);
    uestcradar::Output<uestcradar::RDFrame> output;
    const uestcradar::RDMetadata metadata{
        .channel_index = 0,
        .range_bin_count = 3,
        .doppler_bin_count = 4,
        .range_resolution_m = 1.0,
        .velocity_resolution_mps = 2.0,
    };

    std::uint64_t previous_timestamp = 0;
    for (std::uint64_t expected_id = 1; expected_id <= 2; ++expected_id) {
        auto rd = output.create(metadata);
        std::fill(rd.data().values().begin(), rd.data().values().end(), 2.0F);
        output.write(std::move(rd));

        RingReadLease lease;
        require(
            ringbuf_acquire(downstream.get(), lease) == RingResult::ok,
            "could not read generated RD frame");
        require(
            lease.envelope().frame_id == expected_id &&
                lease.envelope().timestamp >= previous_timestamp &&
                lease.envelope().type_id == kRDTypeId &&
                lease.envelope().type_version == kRDVersion,
            "source trace was not generated monotonically");
        previous_timestamp = lease.envelope().timestamp;
        std::uint32_t reserved{};
        double range_resolution{};
        double velocity_resolution{};
        std::memcpy(&reserved, lease.payload().data() + 12, sizeof(reserved));
        std::memcpy(
            &range_resolution, lease.payload().data() + 16,
            sizeof(range_resolution));
        std::memcpy(
            &velocity_resolution, lease.payload().data() + 24,
            sizeof(velocity_resolution));
        require(
            reserved == 0 &&
                range_resolution == metadata.range_resolution_m &&
                velocity_resolution == metadata.velocity_resolution_mps,
            "generated RD wire metadata is incorrect");
        require(
            ringbuf_release(lease) == RingResult::ok,
            "could not release generated RD frame");
    }
}

void test_contract_rejection(const std::string& prefix) {
    OwnedRing wrong_port{prefix + "_wrong_port", kRDTypeId, kRDVersion};
    ::setenv(
        "UESTCRADAR_UPSTREAM_SHM_NAME", wrong_port.name().c_str(), 1);
    bool port_rejected = false;
    try {
        uestcradar::Input<uestcradar::IQFrame> input;
    } catch (const std::invalid_argument&) {
        port_rejected = true;
    }
    require(port_rejected, "typed Input accepted a different contract");

    OwnedRing wrong_version{
        prefix + "_wrong_version", kIQTypeId, kIQVersion + 1};
    ::setenv(
        "UESTCRADAR_UPSTREAM_SHM_NAME",
        wrong_version.name().c_str(),
        1);
    bool version_rejected = false;
    try {
        uestcradar::Input<uestcradar::IQFrame> input;
    } catch (const std::invalid_argument&) {
        version_rejected = true;
    }
    require(
        version_rejected,
        "typed Input accepted a different contract version");

    OwnedRing malformed{prefix + "_malformed", kIQTypeId, kIQVersion};
    ::setenv(
        "UESTCRADAR_UPSTREAM_SHM_NAME", malformed.name().c_str(), 1);
    RingWriteLease lease;
    require(
        ringbuf_reserve(malformed.get(), lease) == RingResult::ok,
        "could not reserve malformed frame");
    lease.envelope() = uestcradar::Envelope{
        .frame_id = 1,
        .type_id = kIQTypeId,
        .type_version = kIQVersion,
        .payload_length = 1,
    };
    require(
        ringbuf_commit(lease) == RingResult::ok,
        "could not commit malformed frame");
    uestcradar::Input<uestcradar::IQFrame> input;
    bool frame_rejected = false;
    try {
        static_cast<void>(input.read());
    } catch (const std::invalid_argument&) {
        frame_rejected = true;
    }
    require(frame_rejected, "typed Input accepted malformed data");

    OwnedRing rd_output{prefix + "_bad_metadata", kRDTypeId, kRDVersion};
    ::setenv(
        "UESTCRADAR_DOWNSTREAM_SHM_NAME", rd_output.name().c_str(), 1);
    uestcradar::Output<uestcradar::RDFrame> output;
    bool metadata_rejected = false;
    try {
        static_cast<void>(output.create({
            .channel_index = 0,
            .range_bin_count = 0,
            .doppler_bin_count = 4,
            .range_resolution_m = 1.0,
            .velocity_resolution_mps = 2.0,
        }));
    } catch (const std::invalid_argument&) {
        metadata_rejected = true;
    }
    require(metadata_rejected, "typed Output accepted empty dimensions");
}

}  // namespace

int main() {
    try {
        const std::string prefix =
            "/uestcradar_sdk_v5_test_" + std::to_string(::getpid());
        test_typed_input_and_lifetime(prefix);
        test_operator_inherits_trace(prefix);
        test_source_generates_trace(prefix);
        test_contract_rejection(prefix);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk-test: " << error.what() << '\n';
        return 1;
    }
}
