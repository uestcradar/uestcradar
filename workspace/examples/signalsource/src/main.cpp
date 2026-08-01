#include <data.h>

#include "my_waveform.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Options {
    std::string type{"iq"};
    std::uint64_t frames{0};
    double rate_hz{20.0};
};

std::uint64_t parse_uint64(const char* value, const char* option) {
    char* end = nullptr;
    const auto result = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return result;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--type" && index + 1 < argc) {
            options.type = argv[++index];
        } else if (argument == "--frames" && index + 1 < argc) {
            options.frames = parse_uint64(argv[++index], "--frames");
        } else if (argument == "--rate-hz" && index + 1 < argc) {
            options.rate_hz = std::stod(argv[++index]);
        } else {
            throw std::invalid_argument("unknown or incomplete option");
        }
    }
    if ((options.type != "iq" && options.type != "pulse") ||
        options.rate_hz <= 0.0) {
        throw std::invalid_argument("type or rate is invalid");
    }
    return options;
}

std::uint64_t timestamp_ns() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch())
            .count());
}

void send_iq(uestcradar::Output<uestcradar::RawFrame>& output,
             std::uint64_t frame_id) {
    const auto metadata = radar_example::iq_metadata();
    uestcradar::RawFrame raw = output.create({
        .frame_id = frame_id,
        .timestamp = timestamp_ns(),
        .type_id = uestcradar::IQFrameView::type_id,
        .type_version = uestcradar::IQFrameView::type_version,
        .payload_length = static_cast<std::uint32_t>(
            uestcradar::IQFrameView::payload_bytes(metadata)),
    });
    auto iq = uestcradar::IQFrameView::initialize(raw, metadata);
    radar_example::fill_iq(iq);
    output.write(std::move(raw));
}

void send_pulse(uestcradar::Output<uestcradar::RawFrame>& output,
                std::uint64_t frame_id) {
    const auto metadata = radar_example::pulse_metadata(frame_id);
    uestcradar::RawFrame raw = output.create({
        .frame_id = frame_id,
        .timestamp = timestamp_ns(),
        .type_id = uestcradar::PulseCompressionFrameView::type_id,
        .type_version = uestcradar::PulseCompressionFrameView::type_version,
        .payload_length = static_cast<std::uint32_t>(
            uestcradar::PulseCompressionFrameView::payload_bytes(metadata)),
    });
    auto pulse =
        uestcradar::PulseCompressionFrameView::initialize(raw, metadata);
    radar_example::fill_pulse(pulse);
    output.write(std::move(raw));
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const Options options = parse_options(argc, argv);
        uestcradar::Output<uestcradar::RawFrame> output;
        const auto interval = std::chrono::duration<double>(
            1.0 / options.rate_hz);

        std::cout << "[source] type=" << options.type
                  << " rate_hz=" << options.rate_hz
                  << " frames=" << options.frames << '\n';

        for (std::uint64_t frame_id = 1;
             options.frames == 0 || frame_id <= options.frames;
             ++frame_id) {
            if (options.type == "iq") {
                send_iq(output, frame_id);
            } else {
                send_pulse(output, frame_id);
            }

            if (frame_id == 1 || frame_id % 20 == 0) {
                std::cout << "[source] sent type=" << options.type
                          << " frame=" << frame_id << '\n';
            }
            std::this_thread::sleep_for(interval);
        }

        std::cout << "[source] completed frames=" << options.frames << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[source] error=" << error.what() << '\n';
        return 1;
    }
}
