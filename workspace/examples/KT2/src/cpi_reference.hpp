#pragma once

#include <data.h>

#include "sha256.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace radar_example {

inline constexpr std::size_t kExpectedCpiCount = 10;
inline constexpr std::uint32_t kExpectedSamples = 751206;
inline constexpr std::uint32_t kExpectedPulses = 64;
inline constexpr std::size_t kExpectedIQPayloadBytes = 3006960;

struct CpiReference {
    std::string_view metadata_sha256;
    std::string_view input_sha256;
    std::string_view output_sha256;
};

inline constexpr std::array<CpiReference, kExpectedCpiCount> kReferences{{
    {"b886d062a738c8be511795c3ffcd10112b5b46e8abbff3870e9f040fe77ba9c4", "80040b073936723d97291639bc0164598369e892536aaa906c9934e4932e29ce", "3446632a9c20122dcb5089ce2d6a11471545a5a18a277eacbc22f8c90b436fd0"},
    {"fa72fc372dc9e1a51c58f70c7ccd38cfb758d71816b77f5e81b31ecdac0f49e5", "0d06835b583b9cc67e10783eaa231b6a03bf5424c7c3956b74ac13a59294fa30", "03f8dd0f35131a35753804e60618541551ff0ec07d342f9285d48d8353329159"},
    {"d7f6acb565f31f98e0b9610244a28de2d1caab253add792c44ee5cb2800e3361", "03b12e06ef0292657bca02494cd40b452a67396be71c4011f90de35bf66024f9", "c2a8db91fc448ed6a5fa069519a2ec0063c8b48c02238eae04ede808242d2a66"},
    {"9a1177fb302693d4dc866e1f3583c03ddac7a6f97f51f21c3af148511c3a4e8d", "c00eb8d05f55d4fa910cc2bc71bea70b89611b5ef34cc653cac983a009f4408e", "f9605fb0e0c3c5f3ac58bd9c409699a062c9c0ae422ac6b9ff6c8633f0062088"},
    {"26aeedc83e647c96d14568d2659c63fb5833fad59a256eee332bbf46b4ae88d6", "fadcd745bb278ac2e82627e0fd40c9c6f40887569ff86d7bc624e59c00946afc", "77cc78c1ece416ed178c2e37cd5b700110f0b1b72e9956dbccd616a211aad3fc"},
    {"1cc304df5f6a7fd901db52c4196b5bed8dfe6f30f45b4b683d1a7780b652e1d6", "f37f29f7174c7087c39fdfb293d59f76434e43bc8e12b6eccf57982087aa2906", "c5db829ecab78bfae6dd72620a5a4a4c529ff2c83297873d2ba325aa955f41bd"},
    {"817fb8432b7468244081d93f147e7b7e9939971ddda2f99c80a17d2ec9b80d1b", "b52596b1916180d6395d0dac1975b1683adeffe231575977618a1e7079d14e40", "b588abdca5d6e1428597060fac6d6edac2a8e56ac006e8ab6452e5b374d9b0d6"},
    {"0b92c5bb9b12cbdfbcf9606e77091d9b1a034c4029f5f39302a9e12193a32e3c", "fa06c0e009e390def80250f911e625caced14b60ef6ac766863274f048028c33", "1d7dd29eb307c3583678980018c9e8f941027e2f40cd5405a854ac7573e204ea"},
    {"d87fa37bec9627458d77fbd97495824ae694ce6d1f77f8fbdf9730c771a00a45", "a36ef51086e490f179b5bebb9f229240dea0437e298b3cd3b4b02d8b6d665165", "6c3f58de641c117821b65750257655d58fee0b24b291c26d80f1b9cd0dd107c3"},
    {"df2c20b84c7773e0f96c3060c299ebbecf4a9b6dbcac76c9cdec5659a9380fb1", "62e0b210b059847f68aca17970682f3f386805513a50acb133112cef4be6430e", "09bf877820661fa3d8daa853b5d581f079b807a8cde1817a3d8152ec8abbcc45"},
}};

template <class T>
inline void hash_object(Sha256& digest, const T& value) {
    digest.update_object(value);
}

template <class T, std::size_t Size>
inline void hash_array(Sha256& digest, const std::array<T, Size>& values) {
    digest.update(std::as_bytes(std::span{values}));
}

inline std::string metadata_sha256(const uestcradar::IQMetadata& metadata) {
    static_assert(std::endian::native == std::endian::little);
    Sha256 digest;
    hash_object(digest, metadata.channel_count);
    hash_object(digest, metadata.samples_per_channel);
    hash_object(digest, metadata.pulse_count);
    hash_object(digest, metadata.wave_process_type);
    hash_object(digest, metadata.velocity_oversampling);
    const std::uint32_t reserved = 0;
    hash_object(digest, reserved);
    hash_object(digest, metadata.sample_rate_hz);
    hash_object(digest, metadata.nominal_carrier_frequency_hz);
    hash_object(digest, metadata.bandwidth_hz);
    hash_object(digest, metadata.pulse_width_s);
    hash_object(digest, metadata.nominal_prt_s);
    hash_object(digest, metadata.observation_max_range_m);
    hash_object(digest, metadata.dequantization_scale);
    hash_array(digest, metadata.pulse_time_offset_s);
    hash_array(digest, metadata.pulse_phase_rad);
    hash_array(digest, metadata.pulse_frequency_hz);
    hash_array(digest, metadata.coherent_weight);
    return digest.hex();
}

inline std::string sample_sha256(
    std::span<const uestcradar::ComplexInt16> samples) {
    return sha256(std::as_bytes(samples));
}

inline std::string output_sha256(
    std::span<const uestcradar::ComplexFloat32> samples) {
    return sha256(std::as_bytes(samples));
}

class InputVerifier {
public:
    std::size_t verify(const uestcradar::IQFrame& frame) {
        const auto metadata = frame.metadata();
        if (metadata.channel_count != 1 ||
            metadata.samples_per_channel != kExpectedSamples ||
            metadata.pulse_count != kExpectedPulses ||
            frame.data().rows() != 1 ||
            frame.data().columns() != kExpectedSamples) {
            throw std::runtime_error("IQ v3 dimensions are not the CPI reference shape");
        }
        if (previous_cpi_ &&
            (*previous_cpi_ == std::numeric_limits<std::uint64_t>::max() ||
             metadata.cpi_index != *previous_cpi_ + 1)) {
            throw std::runtime_error("IQ cpi_index is not continuous");
        }
        const auto offline_cpi = static_cast<std::size_t>(
            metadata.cpi_index % kExpectedCpiCount);
        const auto& reference = kReferences[offline_cpi];
        if (metadata_sha256(metadata) != reference.metadata_sha256) {
            throw std::runtime_error(
                "CPI" + std::to_string(offline_cpi) +
                " IQ metadata SHA-256 mismatch");
        }
        if (sample_sha256(frame.data().values()) != reference.input_sha256) {
            throw std::runtime_error(
                "CPI" + std::to_string(offline_cpi) +
                " CS16 SHA-256 mismatch");
        }
        previous_cpi_ = metadata.cpi_index;
        return offline_cpi;
    }

private:
    std::optional<std::uint64_t> previous_cpi_;
};

}  // namespace radar_example
