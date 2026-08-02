#include "pc_generator.hpp"
#include "rd_verifier.hpp"

#include <data.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function>
void require_throws(Function&& function, const char* message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

bool equal_samples(
    std::span<const uestcradar::ComplexFloat32> left,
    std::span<const uestcradar::ComplexFloat32> right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].i != right[index].i ||
            left[index].q != right[index].q) {
            return false;
        }
    }
    return true;
}

void test_random_generator() {
    radar_qt_example::RandomPulseGenerator left(1234);
    radar_qt_example::RandomPulseGenerator right(1234);
    radar_qt_example::RandomPulseGenerator other(5678);
    std::vector<uestcradar::ComplexFloat32> left_values(32);
    std::vector<uestcradar::ComplexFloat32> right_values(32);
    std::vector<uestcradar::ComplexFloat32> other_values(32);
    left.fill(left_values);
    right.fill(right_values);
    other.fill(other_values);
    require(equal_samples(left_values, right_values),
            "fixed source seed is not reproducible");
    require(!equal_samples(left_values, other_values),
            "different source seeds produced the same sequence");
}

void test_pulse_metadata() {
    for (std::uint32_t pulse = 0;
         pulse < radar_qt_example::kPulsesPerCpi;
         ++pulse) {
        const auto metadata = radar_qt_example::describe_pulse(pulse);
        require(metadata.channel_count == 1 &&
                    metadata.range_bin_count == 108375 &&
                    metadata.pulse_index == pulse &&
                    metadata.pulses_per_cpi == 64 &&
                    metadata.range_resolution_m == 1.376,
                "generated PulseCompression metadata is invalid");
    }
}

void test_rd_verifier_and_sha256() {
    const std::string abc{"abc"};
    require(
        radar_qt_example::sha256(std::as_bytes(std::span{abc})) ==
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad",
        "SHA-256 implementation failed its known-answer test");

    const uestcradar::RDMetadata metadata{
        .channel_index = 0,
        .range_bin_count = radar_qt_example::kRangeBinCount,
        .doppler_bin_count = radar_qt_example::kDopplerBinCount,
        .range_resolution_m = radar_qt_example::kRangeResolutionM,
        .velocity_resolution_mps =
            radar_qt_example::kVelocityResolutionMps,
    };
    std::vector<float> samples(radar_qt_example::kRdSampleCount, 1.0F);
    const auto digest = radar_qt_example::verify_rd_frame(
        metadata,
        samples,
        radar_qt_example::kRangeBinCount,
        radar_qt_example::kDopplerBinCount);
    require(digest.size() == 64, "RD fingerprint is not SHA-256");

    auto invalid = metadata;
    invalid.doppler_bin_count = 64;
    require_throws(
        [&] {
            static_cast<void>(radar_qt_example::verify_rd_frame(
                invalid,
                samples,
                radar_qt_example::kRangeBinCount,
                radar_qt_example::kDopplerBinCount));
        },
        "invalid RD metadata was accepted");
}

}  // namespace

int main() {
    try {
        test_random_generator();
        test_pulse_metadata();
        test_rd_verifier_and_sha256();
        std::cout << "infra-test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "infra-test: FAIL " << error.what() << '\n';
        return 1;
    }
}
