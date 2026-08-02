#include "preview.hpp"

#include "preview_generated.h"
#include "raw_frame.hpp"

#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace fb = uestcradar::preview;

namespace {

struct IQMetadata {
    std::uint32_t channels;
    std::uint32_t samples;
    double sample_rate;
    double center_frequency;
};

struct ComplexI16 {
    std::int16_t i;
    std::int16_t q;
};

struct PulseMetadata {
    std::uint32_t channels;
    std::uint32_t range_bins;
    std::uint32_t pulse_index;
    std::uint32_t pulses_per_cpi;
    double range_resolution;
};

struct ComplexF32 {
    float i;
    float q;
};

struct RDMetadata {
    std::uint32_t channel_index;
    std::uint32_t range_bins;
    std::uint32_t doppler_bins;
    std::uint32_t reserved;
    double range_resolution;
    double velocity_resolution;
};

std::vector<std::byte> iq_frame() {
    constexpr std::uint32_t channels = 2;
    constexpr std::uint32_t samples = 256;
    const std::size_t payload_bytes =
        sizeof(IQMetadata) + channels * samples * sizeof(ComplexI16);
    std::vector<std::byte> frame(sizeof(uestcradar::Envelope) + payload_bytes);
    const uestcradar::Envelope envelope{
        .frame_id = 77,
        .timestamp = 88,
        .type_id = 1,
        .type_version = 2,
        .payload_length = static_cast<std::uint32_t>(payload_bytes),
    };
    const IQMetadata metadata{channels, samples, 1.0e6, 10.0e9};
    std::memcpy(frame.data(), &envelope, sizeof(envelope));
    std::memcpy(frame.data() + sizeof(envelope), &metadata, sizeof(metadata));
    auto* values = reinterpret_cast<ComplexI16*>(
        frame.data() + sizeof(envelope) + sizeof(metadata));
    for (std::size_t index = 0; index < samples; ++index) {
        values[index] = {static_cast<std::int16_t>(index % 31), 0};
        values[samples + index] = {
            static_cast<std::int16_t>(1000 + index),
            static_cast<std::int16_t>(-2000 - index),
        };
    }
    values[17] = {30000, 0};
    values[samples + 93] = {-32000, 0};
    return frame;
}

bool verify_iq() {
    const auto frame = iq_frame();
    const auto encoded = sidecar::preview::encode_frame_for_test(
        frame, sidecar::preview::Leg::output);
    flatbuffers::Verifier verifier{encoded.data(), encoded.size()};
    if (!fb::VerifyPreviewMessageBuffer(verifier)) {
        return false;
    }
    const auto* message = fb::GetPreviewMessage(encoded.data());
    const auto* preview = message->payload_as_PreviewFrame();
    const auto* waveform = preview == nullptr
        ? nullptr
        : preview->body_as_WaveformPreview();
    if (preview == nullptr || waveform == nullptr ||
        preview->leg() != fb::Leg::Output ||
        preview->original_rows() != 2 ||
        preview->original_columns() != 256 ||
        preview->pool_columns() != 2 ||
        waveform->channels() == nullptr ||
        waveform->channels()->size() != 2) {
        return false;
    }
    const auto* first = waveform->channels()->Get(0);
    const auto* second = waveform->channels()->Get(1);
    return first->channel_index() == 0 && second->channel_index() == 1 &&
           first->bucket_count() == 2 && second->bucket_count() == 2 &&
           first->scale() != second->scale() &&
           first->max_offsets()->Get(0) == 17 &&
           second->max_offsets()->Get(0) == 93 &&
           first->values()->size() == 8 && second->values()->size() == 8;
}

bool verify_large_multichannel_iq_compression() {
    constexpr std::uint32_t channels = 4;
    constexpr std::uint32_t samples = 1'277'952;
    const std::size_t payload_bytes =
        sizeof(IQMetadata) + channels * samples * sizeof(ComplexI16);
    std::vector<std::byte> frame(sizeof(uestcradar::Envelope) + payload_bytes);
    const uestcradar::Envelope envelope{
        .frame_id = 78,
        .timestamp = 89,
        .type_id = 1,
        .type_version = 2,
        .payload_length = static_cast<std::uint32_t>(payload_bytes),
    };
    const IQMetadata metadata{channels, samples, 1.0e6, 10.0e9};
    std::memcpy(frame.data(), &envelope, sizeof(envelope));
    std::memcpy(frame.data() + sizeof(envelope), &metadata, sizeof(metadata));
    auto* values = reinterpret_cast<ComplexI16*>(
        frame.data() + sizeof(envelope) + sizeof(metadata));
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        values[static_cast<std::size_t>(channel) * samples +
               (channel + 1) * 1000] = {
            static_cast<std::int16_t>(20'000 + channel * 1000),
            static_cast<std::int16_t>(-20'000 - channel * 1000),
        };
    }
    const auto encoded = sidecar::preview::encode_frame_for_test(
        frame, sidecar::preview::Leg::output);
    const auto* preview =
        fb::GetPreviewMessage(encoded.data())->payload_as_PreviewFrame();
    const auto* waveform = preview == nullptr
        ? nullptr
        : preview->body_as_WaveformPreview();
    return payload_bytes - sizeof(metadata) == 19.5 * 1024 * 1024 &&
           encoded.size() < frame.size() / 64 &&
           preview != nullptr && waveform != nullptr &&
           preview->original_rows() == channels &&
           preview->original_columns() == samples &&
           preview->pool_columns() == (samples + 127) / 128 &&
           waveform->channels()->size() == channels;
}

bool rejects_truncated_frame() {
    auto frame = iq_frame();
    frame.pop_back();
    try {
        static_cast<void>(sidecar::preview::encode_frame_for_test(
            frame, sidecar::preview::Leg::input));
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

bool verify_pulse_channels() {
    constexpr std::uint32_t channels = 2;
    constexpr std::uint32_t bins = 256;
    const PulseMetadata metadata{channels, bins, 3, 8, 1.5};
    const std::size_t payload_bytes =
        sizeof(metadata) + channels * bins * sizeof(ComplexF32);
    std::vector<std::byte> frame(sizeof(uestcradar::Envelope) + payload_bytes);
    const uestcradar::Envelope envelope{
        .frame_id = 90,
        .timestamp = 91,
        .type_id = 2,
        .type_version = 2,
        .payload_length = static_cast<std::uint32_t>(payload_bytes),
    };
    std::memcpy(frame.data(), &envelope, sizeof(envelope));
    std::memcpy(frame.data() + sizeof(envelope), &metadata, sizeof(metadata));
    auto* values = reinterpret_cast<ComplexF32*>(
        frame.data() + sizeof(envelope) + sizeof(metadata));
    values[17] = {100.0F, 0.0F};
    values[bins + 93] = {0.0F, -200.0F};

    const auto encoded = sidecar::preview::encode_frame_for_test(
        frame, sidecar::preview::Leg::input);
    const auto* preview =
        fb::GetPreviewMessage(encoded.data())->payload_as_PreviewFrame();
    const auto* waveform = preview == nullptr
        ? nullptr
        : preview->body_as_WaveformPreview();
    return preview != nullptr && waveform != nullptr &&
           preview->encoding() == fb::ValueEncoding::ComplexFloat16 &&
           waveform->channels()->size() == 2 &&
           waveform->channels()->Get(0)->channel_index() == 0 &&
           waveform->channels()->Get(1)->channel_index() == 1 &&
           waveform->channels()->Get(0)->max_offsets()->Get(0) == 17 &&
           waveform->channels()->Get(1)->max_offsets()->Get(0) == 93;
}

bool verify_rd_channel_and_pooling() {
    constexpr std::uint32_t rows = 16;
    constexpr std::uint32_t columns = 16;
    const RDMetadata metadata{3, rows, columns, 0, 1.5, 2.5};
    const std::size_t payload_bytes =
        sizeof(metadata) + rows * columns * sizeof(float);
    std::vector<std::byte> frame(sizeof(uestcradar::Envelope) + payload_bytes);
    const uestcradar::Envelope envelope{
        .frame_id = 100,
        .timestamp = 101,
        .type_id = 3,
        .type_version = 2,
        .payload_length = static_cast<std::uint32_t>(payload_bytes),
    };
    std::memcpy(frame.data(), &envelope, sizeof(envelope));
    std::memcpy(frame.data() + sizeof(envelope), &metadata, sizeof(metadata));
    auto* values = reinterpret_cast<float*>(
        frame.data() + sizeof(envelope) + sizeof(metadata));
    values[2 * columns + 5] = -50.0F;
    values[11 * columns + 12] = 80.0F;

    const auto encoded = sidecar::preview::encode_frame_for_test(
        frame, sidecar::preview::Leg::output);
    const auto* preview =
        fb::GetPreviewMessage(encoded.data())->payload_as_PreviewFrame();
    const auto* heatmap = preview == nullptr
        ? nullptr
        : preview->body_as_HeatmapPreview();
    return preview != nullptr && heatmap != nullptr &&
           preview->encoding() == fb::ValueEncoding::Float16 &&
           preview->pool_rows() == 2 && preview->pool_columns() == 2 &&
           heatmap->channel_index() == 3 &&
           heatmap->rows() == 2 && heatmap->columns() == 2 &&
           heatmap->max_offsets()->Get(0) == 21 &&
           heatmap->max_offsets()->Get(3) == 28;
}

}  // namespace

int main() {
    if (!verify_iq() || !verify_large_multichannel_iq_compression() ||
        !verify_pulse_channels() ||
        !verify_rd_channel_and_pooling() || !rejects_truncated_frame()) {
        std::cerr << "preview-test: failed\n";
        return 1;
    }
    std::cout << "preview-test: passed\n";
    return 0;
}
