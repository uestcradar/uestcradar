#pragma once
#include "cpi_buffer.h"
#include <QByteArray>
#include <QDataStream>
#include <limits>

namespace radar_qt_example {
struct AlgorithmResult {
    int frequencies{};
    int ranges{};
    std::vector<double> values; // [frequency][range]
};
struct RdOutput {
    uestcradar::RDMetadata metadata;
    std::vector<float> values; // [range][frequency]
};
namespace frame_converter {
inline QByteArray to_algorithm_frame(const CpiBuffer& cpi) {
    if (!cpi.ready()) throw std::invalid_argument("Incomplete CPI");
    static constexpr unsigned char magic[16] = {
        0xBC,0x1C,0xAA,0xFF,0xFF,0xFF,0xAA,0xFF,0xFF,0x7F,0xFF,0x7F,0xFF,0x7F,0xFF,0x7F};
    QByteArray bytes;
    bytes.reserve(20 + kPulsesPerCpi * cpi.range_bin_count() * 8);
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream.writeRawData(reinterpret_cast<const char*>(magic), 16);
    stream << quint16(cpi.range_bin_count()) << quint16(kPulsesPerCpi);
    for (std::size_t pulse=0; pulse<kPulsesPerCpi; ++pulse) {
        for (std::size_t range=0; range<cpi.range_bin_count(); ++range) {
            const auto& sample=cpi.sample(pulse, range);
            if (!std::isfinite(sample.i) || !std::isfinite(sample.q))
                throw std::invalid_argument("Non-finite input sample");
            stream << sample.i << sample.q;
        }
    }
    if (stream.status()!=QDataStream::Ok) throw std::runtime_error("CPI serialization failed");
    return bytes;
}
inline RdOutput to_rd_frame(const AlgorithmResult& result, const CpiBuffer& cpi) {
    if (!cpi.ready() || result.ranges<=0 || result.frequencies<=0)
        throw std::invalid_argument("Invalid algorithm dimensions");
    const auto cells=static_cast<std::size_t>(result.ranges)*result.frequencies;
    if (cells>(kMaxFrameBytes-kRdMetadataBytes)/sizeof(float) || cells!=result.values.size())
        throw std::invalid_argument("Invalid algorithm payload length");
    RdOutput output{{.channel_index=0,
        .range_bin_count=static_cast<std::uint32_t>(result.ranges),
        .doppler_bin_count=static_cast<std::uint32_t>(result.frequencies),
        .range_resolution_m=cpi.range_resolution_m(),
        .velocity_resolution_mps=kVelocityResolutionMps}, std::vector<float>(cells)};
    for (int f=0; f<result.frequencies; ++f) {
        for (int r=0; r<result.ranges; ++r) {
            const double value=result.values[static_cast<std::size_t>(f)*result.ranges+r];
            if (!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max())
                throw std::invalid_argument("Algorithm value cannot be represented as float");
            output.values[static_cast<std::size_t>(r)*result.frequencies+f]=static_cast<float>(value);
        }
    }
    return output;
}
} // namespace frame_converter
} // namespace radar_qt_example
