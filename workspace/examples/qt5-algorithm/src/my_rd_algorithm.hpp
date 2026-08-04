#pragma once

#include "rd_contract.hpp"

#include <data.h>

#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <vector>

namespace radar_qt_example {

class CpiBuffer {
public:
    void push(
        const uestcradar::PulseCompressionMetadata& metadata,
        std::span<const uestcradar::ComplexFloat32> range_bins) {
        validate_frame(metadata, range_bins);

        if (metadata.pulse_index == 0 && received_pulses_ != 0) {
            clear();
        }
        if (received_pulses_ == 0) {
            if (metadata.pulse_index != 0) {
                return;
            }
            begin_cpi(metadata);
        } else if (!matches_current_cpi(metadata) ||
                   metadata.pulse_index != received_pulses_) {
            clear();
            return;
        }

        const auto destination = samples_.begin() +
            static_cast<std::ptrdiff_t>(
                metadata.pulse_index * range_bin_count_);
        std::copy(range_bins.begin(), range_bins.end(), destination);
        ++received_pulses_;
    }

    [[nodiscard]] bool ready() const noexcept {
        return received_pulses_ == kPulsesPerCpi;
    }

    [[nodiscard]] std::uint32_t range_bin_count() const noexcept {
        return range_bin_count_;
    }

    [[nodiscard]] double range_resolution_m() const noexcept {
        return range_resolution_m_;
    }

    [[nodiscard]] const uestcradar::ComplexFloat32& sample(
        std::size_t pulse,
        std::size_t range) const {
        if (!ready() || pulse >= kPulsesPerCpi ||
            range >= range_bin_count_) {
            throw std::out_of_range("CPI sample is unavailable");
        }
        return samples_.at(static_cast<int>(
            pulse * range_bin_count_ + range));
    }

    void clear() {
        samples_.clear();
        range_bin_count_ = 0;
        range_resolution_m_ = 0.0;
        received_pulses_ = 0;
    }

private:
    static void validate_frame(
        const uestcradar::PulseCompressionMetadata& metadata,
        std::span<const uestcradar::ComplexFloat32> range_bins) {
        if (metadata.channel_count != kChannelCount ||
            metadata.pulses_per_cpi != kPulsesPerCpi ||
            metadata.pulse_index >= kPulsesPerCpi ||
            metadata.range_bin_count == 0 ||
            metadata.range_bin_count > kMaxRangeBinCount ||
            !std::isfinite(metadata.range_resolution_m) ||
            metadata.range_resolution_m <= 0.0 ||
            range_bins.size() != metadata.range_bin_count) {
            throw std::invalid_argument(
                "PulseCompressionFrame does not match the developer-base contract");
        }
    }

    void begin_cpi(const uestcradar::PulseCompressionMetadata& metadata) {
        range_bin_count_ = metadata.range_bin_count;
        range_resolution_m_ = metadata.range_resolution_m;
        samples_.resize(static_cast<int>(
            static_cast<std::size_t>(range_bin_count_) * kPulsesPerCpi));
    }

    [[nodiscard]] bool matches_current_cpi(
        const uestcradar::PulseCompressionMetadata& metadata) const noexcept {
        return metadata.range_bin_count == range_bin_count_ &&
            metadata.range_resolution_m == range_resolution_m_;
    }

    QVector<uestcradar::ComplexFloat32> samples_;
    std::uint32_t range_bin_count_{};
    double range_resolution_m_{};
    std::uint32_t received_pulses_{};
};

struct RdResult {
    std::size_t peak_range_bin{};
    std::size_t peak_doppler_bin{};
    float peak_magnitude{};
};

// This is intentionally a fast placeholder, not an FFT/DFT implementation.
// Replace the function body with the real Qt5 Range-Doppler algorithm.
inline RdResult compute_rd(
    const CpiBuffer& pulse_cpi,
    uestcradar::Array2D<float> rd_map) {
    if (!pulse_cpi.ready() ||
        rd_map.rows() != pulse_cpi.range_bin_count() ||
        rd_map.columns() != kDopplerBinCount) {
        throw std::invalid_argument("RD output shape does not match the CPI");
    }

    RdResult result;
    for (std::size_t range = 0; range < rd_map.rows(); ++range) {
        for (std::size_t doppler = 0;
             doppler < rd_map.columns();
             ++doppler) {
            const auto& sample = pulse_cpi.sample(
                doppler % kPulsesPerCpi, range);
            const auto magnitude = std::hypot(sample.i, sample.q);
            rd_map[range][doppler] = magnitude;
            if (magnitude > result.peak_magnitude) {
                result = {range, doppler, magnitude};
            }
        }
    }
    return result;
}

inline void save_rd_map_pgm(
    uestcradar::Array2D<float> rd_map,
    const std::filesystem::path& output_path) {
    if (rd_map.rows() == 0 || rd_map.columns() == 0) {
        throw std::invalid_argument("RD output is empty");
    }

    const auto values = rd_map.values();
    const auto [minimum, maximum] = std::minmax_element(
        values.begin(), values.end());
    if (!std::isfinite(*minimum) || !std::isfinite(*maximum)) {
        throw std::invalid_argument("RD output contains a non-finite value");
    }

    std::vector<unsigned char> pixels(values.size(), 0);
    if (*maximum > *minimum) {
        const float scale = 255.0F / (*maximum - *minimum);
        std::transform(
            values.begin(), values.end(), pixels.begin(),
            [&](float value) {
                return static_cast<unsigned char>(std::clamp(
                    std::lround((value - *minimum) * scale), 0L, 255L));
            });
    }

    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    auto temporary_path = output_path;
    temporary_path += ".tmp";
    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create RDMap PGM output");
    }
    output << "P5\n" << rd_map.columns() << ' ' << rd_map.rows()
           << "\n255\n";
    output.write(
        reinterpret_cast<const char*>(pixels.data()),
        static_cast<std::streamsize>(pixels.size()));
    output.close();
    if (!output) {
        throw std::runtime_error("cannot write RDMap PGM output");
    }
    std::filesystem::rename(temporary_path, output_path);
}

}  // namespace radar_qt_example
