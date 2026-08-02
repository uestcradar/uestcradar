#pragma once

#include <data.h>

#include "echo_process.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace radar_algorithm {

struct AdaptedCpi {
    EchoProcessInput input;
    RadarParams params;
};

inline AdaptedCpi adapt_complete_cpi(
    const uestcradar::IQMetadata& metadata,
    uestcradar::Array2D<const uestcradar::ComplexInt16> samples) {
    if (metadata.channel_count != 1 || samples.rows() != 1 ||
        samples.columns() != metadata.samples_per_channel ||
        metadata.pulse_count == 0 ||
        metadata.pulse_count > uestcradar::kMaxPulsesPerCpi ||
        metadata.sample_rate_hz <= 0.0 ||
        metadata.nominal_carrier_frequency_hz <= 0.0 ||
        metadata.bandwidth_hz <= 0.0 || metadata.pulse_width_s <= 0.0 ||
        metadata.nominal_prt_s <= 0.0 ||
        metadata.observation_max_range_m <= 0.0 ||
        !std::isfinite(metadata.dequantization_scale) ||
        metadata.dequantization_scale <= 0.0 ||
        metadata.velocity_oversampling == 0 ||
        metadata.velocity_oversampling >
            static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        metadata.pulse_count >
            static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        metadata.wave_process_type >
            static_cast<std::uint32_t>(WaveProcessType::RADON_S_T)) {
        throw std::invalid_argument("IQ v3 CPI metadata is invalid");
    }

    AdaptedCpi result;
    result.params.f0 = metadata.nominal_carrier_frequency_hz;
    result.params.B = metadata.bandwidth_hz;
    result.params.tao = metadata.pulse_width_s;
    result.params.PRT = metadata.nominal_prt_s;
    result.params.fs = metadata.sample_rate_hz;
    result.params.pulseN = static_cast<int>(metadata.pulse_count);
    result.params.Rmax = metadata.observation_max_range_m;
    result.params.Nv = static_cast<int>(metadata.velocity_oversampling);
    result.params.waveProcessType =
        static_cast<WaveProcessType>(metadata.wave_process_type);
    result.params.realtimeMode = false;
    result.params.updateDerivedParams();

    const std::size_t pulse_count = metadata.pulse_count;
    result.input.pulseTime.assign(
        metadata.pulse_time_offset_s.begin(),
        metadata.pulse_time_offset_s.begin() + pulse_count);
    result.input.pulsePhase.assign(
        metadata.pulse_phase_rad.begin(),
        metadata.pulse_phase_rad.begin() + pulse_count);
    result.input.pulseFreq.assign(
        metadata.pulse_frequency_hz.begin(),
        metadata.pulse_frequency_hz.begin() + pulse_count);
    result.input.wd0.assign(
        metadata.coherent_weight.begin(),
        metadata.coherent_weight.begin() + pulse_count);
    for (std::size_t pulse = 0; pulse < pulse_count; ++pulse) {
        if (!std::isfinite(result.input.pulseTime[pulse]) ||
            !std::isfinite(result.input.pulsePhase[pulse]) ||
            !std::isfinite(result.input.pulseFreq[pulse]) ||
            !std::isfinite(result.input.wd0[pulse])) {
            throw std::invalid_argument("IQ v3 pulse metadata is not finite");
        }
    }

    result.input.echo.reserve(samples.columns());
    for (const auto& sample : samples[0]) {
        result.input.echo.emplace_back(
            static_cast<double>(sample.i) * metadata.dequantization_scale,
            static_cast<double>(sample.q) * metadata.dequantization_scale);
    }
    return result;
}

inline EchoProcessOutput run_original_algorithm(const AdaptedCpi& cpi) {
    return processEcho(cpi.input, cpi.params);
}

}  // namespace radar_algorithm
