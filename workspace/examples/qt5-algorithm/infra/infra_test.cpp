#include "rd_verifier.hpp"

#include <data.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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

void test_rd_verifier_and_sha256() {
    const std::string abc{"abc"};
    require(
        radar_qt_example::sha256(std::as_bytes(std::span{abc})) ==
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad",
        "SHA-256 implementation failed its known-answer test");

    constexpr std::uint32_t range_bins = 22196;
    const uestcradar::RDMetadata metadata{
        .channel_index = 0,
        .range_bin_count = range_bins,
        .doppler_bin_count = radar_qt_example::kDopplerBinCount,
        .range_resolution_m = 4.8828125,
        .velocity_resolution_mps =
            radar_qt_example::kVelocityResolutionMps,
    };
    std::vector<float> samples(
        static_cast<std::size_t>(range_bins) *
            radar_qt_example::kDopplerBinCount,
        1.0F);
    const std::size_t peak_range = 20480;
    const std::size_t peak_doppler = 32;
    samples[peak_range * radar_qt_example::kDopplerBinCount +
            peak_doppler] = 10.0F;
    const auto verification = radar_qt_example::verify_rd_frame(
        metadata,
        samples,
        range_bins,
        radar_qt_example::kDopplerBinCount);
    require(verification.digest.size() == 64,
            "RD fingerprint is not SHA-256");
    require(verification.peak_range_bin == peak_range &&
                verification.peak_doppler_bin == peak_doppler &&
                verification.peak_magnitude == 10.0F,
            "RD peak location is invalid");

    auto invalid = metadata;
    invalid.doppler_bin_count = 64;
    require_throws(
        [&] {
            static_cast<void>(radar_qt_example::verify_rd_frame(
                invalid,
                samples,
                range_bins,
                radar_qt_example::kDopplerBinCount));
        },
        "invalid RD metadata was accepted");

    samples.front() = std::numeric_limits<float>::infinity();
    require_throws(
        [&] {
            static_cast<void>(radar_qt_example::verify_rd_frame(
                metadata,
                samples,
                range_bins,
                radar_qt_example::kDopplerBinCount));
        },
        "non-finite RD data was accepted");
}

}  // namespace

int main() {
    try {
        test_rd_verifier_and_sha256();
        std::cout << "infra-test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "infra-test: FAIL " << error.what() << '\n';
        return 1;
    }
}
