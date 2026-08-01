#include <data.h>

#include "ringbuf/ringbuf.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <unistd.h>

namespace {

using uestcradar::Envelope;
using uestcradar::RawFrame;
using uestcradar::ComplexInt16;
using uestcradar::IQFrameView;
using uestcradar::IQMetadata;
using uestcradar::PulseCompressionFrameView;
using uestcradar::PulseCompressionMetadata;
using uestcradar::RDFrameView;
using uestcradar::RDMetadata;

class OwnedRing {
public:
    OwnedRing(
        std::string name,
        std::uint64_t type_id,
        std::uint32_t type_version = 2,
        std::uint32_t slots = 2)
        : name_(std::move(name)),
          ring_(ringbuf_create(
              name_.c_str(),
              {slots, 4096, type_id, type_version})) {}

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

void seed_iq(RingBuffer* ring, std::uint64_t frame_id) {
    const IQMetadata metadata{2, 3, 2.5e6, 1.2e9};
    RingWriteLease lease;
    require(
        ringbuf_reserve(ring, lease) == RingResult::ok,
        "could not reserve IQ frame");
    lease.envelope() = {
        .frame_id = frame_id,
        .timestamp = 123456,
        .type_id = IQFrameView::type_id,
        .type_version = IQFrameView::type_version,
        .payload_length = static_cast<std::uint32_t>(
            IQFrameView::payload_bytes(metadata)),
    };
    ::new (lease.payload().data()) IQMetadata{metadata};
    auto* samples = reinterpret_cast<ComplexInt16*>(
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

void test_input_and_view_lifetime(const std::string& prefix) {
    OwnedRing upstream{prefix + "_iq_up", IQFrameView::type_id};
    ::setenv(
        "UESTCRADAR_UPSTREAM_SHM_NAME", upstream.name().c_str(), 1);
    seed_iq(upstream.get(), 42);
    seed_iq(upstream.get(), 43);

    uestcradar::Input<RawFrame> input;
    {
        RawFrame raw = input.read();
        auto view = IQFrameView::from(raw);
        require(
            raw.envelope().frame_id == 42 &&
                view.metadata().channel_count == 2 &&
                view.data().rows() == 2 &&
                view.data().columns() == 3 &&
                view.data()[1][2].i == 6,
            "IQ Contract View mapping is incorrect");
        const auto payload = raw.payload_span();
        const auto* values = reinterpret_cast<const std::byte*>(
            view.data().values().data());
        require(
            values >= payload.data() &&
                values < payload.data() + payload.size(),
            "IQ Contract View copied the sample payload");

        RingWriteLease blocked;
        require(
            ringbuf_reserve(upstream.get(), blocked) ==
                RingResult::would_block,
            "input Slot was reused while RawFrame was alive");
    }

    RingWriteLease reusable;
    require(
        ringbuf_reserve(upstream.get(), reusable) == RingResult::ok,
        "input Slot was not released by RawFrame destructor");
    ringbuf_cancel(reusable);
}

void test_output_commit_and_cancel(const std::string& prefix) {
    OwnedRing downstream{
        prefix + "_pc_down", PulseCompressionFrameView::type_id};
    ::setenv(
        "UESTCRADAR_DOWNSTREAM_SHM_NAME",
        downstream.name().c_str(),
        1);
    uestcradar::Output<RawFrame> output;
    const PulseCompressionMetadata metadata{2, 3, 7, 128, 1.5};
    const Envelope envelope{
        .frame_id = 9,
        .timestamp = 987654,
        .type_id = PulseCompressionFrameView::type_id,
        .type_version = PulseCompressionFrameView::type_version,
        .payload_length = static_cast<std::uint32_t>(
            PulseCompressionFrameView::payload_bytes(metadata)),
    };
    {
        RawFrame raw = output.create(envelope);
        auto view = PulseCompressionFrameView::initialize(raw, metadata);
        const auto payload = raw.payload_span();
        const auto* values = reinterpret_cast<const std::byte*>(
            view.data().values().data());
        require(
            values >= payload.data() &&
                values < payload.data() + payload.size(),
            "pulse compression View copied the sample payload");
        std::fill(
            view.data().values().begin(),
            view.data().values().end(),
            uestcradar::ComplexFloat32{3.0F, 4.0F});
        output.write(std::move(raw));
    }

    RingReadLease committed;
    require(
        ringbuf_acquire(downstream.get(), committed) == RingResult::ok &&
            committed.envelope().frame_id == 9 &&
            committed.payload().size() == envelope.payload_length,
        "output RawFrame was not committed");
    require(
        ringbuf_release(committed) == RingResult::ok,
        "could not release output RawFrame");

    {
        RawFrame abandoned = output.create(envelope);
        auto view = PulseCompressionFrameView::initialize(
            abandoned, metadata);
        view.data()[0][0] = {7.0F, 8.0F};
    }
    RingWriteLease reusable;
    require(
        ringbuf_reserve(downstream.get(), reusable) == RingResult::ok,
        "abandoned RawFrame did not cancel its Slot");
    ringbuf_cancel(reusable);
}

void test_contract_rejection(const std::string& prefix) {
    OwnedRing rd_ring{prefix + "_rd_up", RDFrameView::type_id};
    ::setenv(
        "UESTCRADAR_UPSTREAM_SHM_NAME", rd_ring.name().c_str(), 1);
    const RDMetadata metadata{0, 3, 4, 0, 1.0, 2.0};
    RingWriteLease lease;
    require(
        ringbuf_reserve(rd_ring.get(), lease) == RingResult::ok,
        "could not reserve RD frame");
    lease.envelope() = {
        .frame_id = 1,
        .type_id = RDFrameView::type_id,
        .type_version = RDFrameView::type_version,
        .payload_length = static_cast<std::uint32_t>(
            RDFrameView::payload_bytes(metadata)),
    };
    ::new (lease.payload().data()) RDMetadata{metadata};
    require(ringbuf_commit(lease) == RingResult::ok, "could not seed RD");

    uestcradar::Input<RawFrame> input;
    RawFrame raw = input.read();
    auto valid_view = RDFrameView::from(raw);
    const auto payload = raw.payload_span();
    const auto* values = reinterpret_cast<const std::byte*>(
        valid_view.data().values().data());
    require(
        values >= payload.data() &&
            values < payload.data() + payload.size(),
        "RD View copied the sample payload");

    raw.envelope().type_id = IQFrameView::type_id;
    bool type_rejected = false;
    try {
        static_cast<void>(RDFrameView::from(raw));
    } catch (const std::invalid_argument&) {
        type_rejected = true;
    }
    require(type_rejected, "Contract View accepted wrong type");

    raw.envelope().type_id = RDFrameView::type_id;
    raw.envelope().type_version = 1;
    bool version_rejected = false;
    try {
        static_cast<void>(RDFrameView::from(raw));
    } catch (const std::invalid_argument&) {
        version_rejected = true;
    }
    require(version_rejected, "Contract View accepted wrong version");

    raw.envelope().type_version = RDFrameView::type_version;
    raw.envelope().payload_length -= 1;
    bool length_rejected = false;
    try {
        static_cast<void>(RDFrameView::from(raw));
    } catch (const std::invalid_argument&) {
        length_rejected = true;
    }
    require(length_rejected, "Contract View accepted truncated payload");

    raw.envelope().payload_length += 1;
    auto* stored_metadata = reinterpret_cast<RDMetadata*>(
        raw.payload_span().data());
    stored_metadata->reserved = 1;
    bool reserved_rejected = false;
    try {
        static_cast<void>(RDFrameView::from(raw));
    } catch (const std::invalid_argument&) {
        reserved_rejected = true;
    }
    require(reserved_rejected, "RD View accepted non-zero reserved data");

    bool dimensions_rejected = false;
    try {
        static_cast<void>(RDFrameView::payload_bytes(
            RDMetadata{0, 0, 4, 0, 1.0, 2.0}));
    } catch (const std::invalid_argument&) {
        dimensions_rejected = true;
    }
    require(dimensions_rejected, "RD View accepted empty dimensions");
}

}  // namespace

int main() {
    try {
        const std::string prefix =
            "/uestcradar_sdk_v4_test_" + std::to_string(::getpid());
        test_input_and_view_lifetime(prefix);
        test_output_commit_and_cancel(prefix);
        test_contract_rejection(prefix);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk-test: " << error.what() << '\n';
        return 1;
    }
}
