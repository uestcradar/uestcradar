#pragma once

#include <cstddef>
#include <cstdint>

namespace radar_qt_example {

inline constexpr std::uint32_t kChannelCount = 1;
inline constexpr std::uint32_t kPulsesPerCpi = 64;
inline constexpr std::uint32_t kDopplerBinCount = 65;
inline constexpr double kVelocityResolutionMps = 0.5;

inline constexpr std::size_t kRdMetadataBytes = 32;
inline constexpr std::size_t kFloat32Bytes = 4;
inline constexpr std::size_t kMaxFrameBytes = 32U * 1024U * 1024U;

inline constexpr std::size_t rd_frame_bytes(std::size_t range_bin_count) {
    return kRdMetadataBytes + range_bin_count * kDopplerBinCount *
        kFloat32Bytes;
}

inline constexpr std::size_t kMaxRangeBinCount =
    (kMaxFrameBytes - kRdMetadataBytes) /
    (kDopplerBinCount * kFloat32Bytes);

static_assert(kMaxRangeBinCount == 129055);
static_assert(rd_frame_bytes(kMaxRangeBinCount) <= kMaxFrameBytes);
static_assert(rd_frame_bytes(kMaxRangeBinCount + 1) > kMaxFrameBytes);

}  // namespace radar_qt_example
