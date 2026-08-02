#pragma once

#include <cstddef>
#include <cstdint>

namespace radar_qt_example {

inline constexpr std::uint32_t kChannelCount = 1;
inline constexpr std::uint32_t kPulsesPerCpi = 64;
inline constexpr std::uint32_t kRangeBinCount = 108375;
inline constexpr std::uint32_t kDopplerBinCount = 65;
inline constexpr double kRangeResolutionM = 1.376;
inline constexpr double kVelocityResolutionMps = 0.5;

inline constexpr std::size_t kPulseMetadataBytes = 24;
inline constexpr std::size_t kRdMetadataBytes = 32;
inline constexpr std::size_t kComplexFloat32Bytes = 8;
inline constexpr std::size_t kFloat32Bytes = 4;
inline constexpr std::size_t kMaxFrameBytes = 32U * 1024U * 1024U;

inline constexpr std::size_t kPulseSampleCount =
    static_cast<std::size_t>(kChannelCount) * kRangeBinCount;
inline constexpr std::size_t kPulseFrameBytes =
    kPulseMetadataBytes + kPulseSampleCount * kComplexFloat32Bytes;
inline constexpr std::size_t kCpiSampleCount =
    static_cast<std::size_t>(kPulsesPerCpi) * kRangeBinCount;
inline constexpr std::size_t kRdSampleCount =
    static_cast<std::size_t>(kRangeBinCount) * kDopplerBinCount;
inline constexpr std::size_t kRdFrameBytes =
    kRdMetadataBytes + kRdSampleCount * kFloat32Bytes;

static_assert(kPulseFrameBytes == 867024);
static_assert(kRdFrameBytes == 28177532);
static_assert(kRdFrameBytes <= kMaxFrameBytes);

}  // namespace radar_qt_example
