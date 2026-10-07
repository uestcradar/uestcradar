#include "cpi_reference.hpp"
#include "my_pulsecompression.hpp"
#include "runtime_options.hpp"
#include "sha256.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_sha256() {
    const std::string input{"abc"};
    const auto hash = radar_example::sha256(std::as_bytes(std::span{input}));
    require(
        hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 golden value mismatch");
}

void test_payload_boundary() {
    require(
        radar_example::pulse_payload_bytes(
            1, radar_example::kExpectedSamples) == 6009672,
        "reference output payload size mismatch");
    constexpr std::uint32_t maximum_elements =
        static_cast<std::uint32_t>(
            (radar_example::kMaxFramePayloadBytes -
             radar_example::kPulseMetadataBytes) /
            sizeof(uestcradar::ComplexFloat32));
    require(
        radar_example::pulse_payload_bytes(1, maximum_elements) <=
            radar_example::kMaxFramePayloadBytes,
        "maximum payload should fit");
    bool rejected = false;
    try {
        static_cast<void>(radar_example::pulse_payload_bytes(
            1, maximum_elements + 1));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "oversized payload was not rejected");
}

void test_dequantization() {
    std::array<uestcradar::ComplexInt16, 3> input{{
        {3000, -1500}, {-32768, 32767}, {0, 1},
    }};
    std::array<uestcradar::ComplexFloat32, 3> output{};
    constexpr double scale = 1.0 / 3000.0;
    radar_example::dequantize_samples(
        {input.data(), 1, input.size()},
        {output.data(), 1, output.size()}, scale);
    require(
        output[0].i == 1.0F && output[0].q == -0.5F &&
            output[1].i == static_cast<float>(-32768 * scale) &&
            output[1].q == static_cast<float>(32767 * scale) &&
            output[2].i == 0.0F &&
            output[2].q == static_cast<float>(scale),
        "CS16 dequantization mismatch");
}

void test_reference_uniqueness() {
    for (std::size_t left = 0;
         left < radar_example::kReferences.size(); ++left) {
        for (std::size_t right = left + 1;
             right < radar_example::kReferences.size(); ++right) {
            require(
                radar_example::kReferences[left].metadata_sha256 !=
                    radar_example::kReferences[right].metadata_sha256,
                "CPI metadata fingerprints must be unique");
            require(
                radar_example::kReferences[left].input_sha256 !=
                    radar_example::kReferences[right].input_sha256,
                "CPI input fingerprints must be unique");
            require(
                radar_example::kReferences[left].output_sha256 !=
                    radar_example::kReferences[right].output_sha256,
                "CPI output fingerprints must be unique");
        }
    }
}

}  // namespace

int main() {
    try {
        test_sha256();
        test_payload_boundary();
        test_dequantization();
        test_reference_uniqueness();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "pulsecompression-test: " << error.what() << '\n';
        return 1;
    }
}
