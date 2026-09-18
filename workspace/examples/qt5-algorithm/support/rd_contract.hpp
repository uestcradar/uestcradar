#pragma once
#include <cstddef>
#include <cstdint>
namespace radar_qt_example {
inline constexpr std::uint32_t kChannelCount = 1;
inline constexpr std::uint32_t kPulsesPerCpi = 64;
inline constexpr double kVelocityResolutionMps = 0.5;
inline constexpr std::size_t kRdMetadataBytes = 32;
inline constexpr std::size_t kMaxFrameBytes = 32U * 1024U * 1024U;
inline constexpr std::size_t kInputShmBytes = 32U * 1024U * 1024U;
// The vendor parser rejects dimensions above 60000, even if SHM has room.
inline constexpr std::uint32_t kMaxInputRangeBinCount = 60000;
static_assert(20 + std::size_t(kPulsesPerCpi) * kMaxInputRangeBinCount * 8 <= kInputShmBytes - 16);
inline constexpr std::size_t rd_frame_bytes(std::size_t ranges, std::size_t frequencies) {
    return kRdMetadataBytes + ranges * frequencies * sizeof(float);
}
} // namespace radar_qt_example
