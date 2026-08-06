#include <data.h>

#include "cpi_reference.hpp"
#include "my_pulsecompression.hpp"
#include "runtime_options.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const auto options = radar_example::RuntimeOptions::parse(argc, argv);
        uestcradar::Input<uestcradar::IQFrame> input;
        uestcradar::Output<uestcradar::PulseCompressionFrame> output;
        radar_example::InputVerifier verifier;

        std::cout << "[pulsecompression] IQ v3 developer base ready"
                  << " max_frame_bytes="
                  << radar_example::kMaxFramePayloadBytes
                  << " frames=" << options.frames << '\n';

        std::uint64_t processed = 0;
        while (options.frames == 0 || processed < options.frames) {
            // 1. Read one complete CPI from the upstream Sidecar.
            auto iq = input.read();
            const auto offline_cpi = verifier.verify(iq);

            // 2. Describe and create exactly one output frame.
            auto pulse = output.create(
                radar_example::describe_output(iq.metadata()), iq);

            // 3. Replace this function with the real pulse-compression math.
            radar_example::dequantize(iq, pulse);

            // 4. Publish the finished frame to the downstream Sidecar.
            output.write(std::move(pulse));

            ++processed;
            if (processed == 1 || processed % options.log_every == 0) {
                std::cout << "[pulsecompression] PASS processed="
                          << processed << " offline_cpi=CPI"
                          << offline_cpi << " samples="
                          << radar_example::kExpectedSamples << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[pulsecompression] FAIL " << error.what() << '\n';
        return 1;
    }
}
