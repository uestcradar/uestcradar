#include "cpi_data.hpp"
#include "iq_adapter.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

EchoProcessInput make_reference_input(const radar_example::CpiData& cpi) {
    EchoProcessInput input;
    const std::size_t pulse_count = cpi.metadata.pulse_count;
    input.pulseTime.assign(
        cpi.metadata.pulse_time_offset_s.begin(),
        cpi.metadata.pulse_time_offset_s.begin() + pulse_count);
    input.pulsePhase.assign(
        cpi.metadata.pulse_phase_rad.begin(),
        cpi.metadata.pulse_phase_rad.begin() + pulse_count);
    input.pulseFreq.assign(
        cpi.metadata.pulse_frequency_hz.begin(),
        cpi.metadata.pulse_frequency_hz.begin() + pulse_count);
    input.wd0.assign(
        cpi.metadata.coherent_weight.begin(),
        cpi.metadata.coherent_weight.begin() + pulse_count);
    std::vector<uestcradar::ComplexInt16> samples(
        cpi.metadata.samples_per_channel);
    std::memcpy(samples.data(), cpi.cs16.data(), cpi.cs16.size());
    input.echo.reserve(cpi.metadata.samples_per_channel);
    for (std::size_t index = 0;
         index < cpi.metadata.samples_per_channel;
         ++index) {
        input.echo.emplace_back(
            static_cast<double>(samples[index].i) *
                cpi.metadata.dequantization_scale,
            static_cast<double>(samples[index].q) *
                cpi.metadata.dequantization_scale);
    }
    return input;
}

RadarParams make_reference_params(const uestcradar::IQMetadata& metadata) {
    RadarParams params;
    params.f0 = metadata.nominal_carrier_frequency_hz;
    params.B = metadata.bandwidth_hz;
    params.tao = metadata.pulse_width_s;
    params.PRT = metadata.nominal_prt_s;
    params.fs = metadata.sample_rate_hz;
    params.pulseN = static_cast<int>(metadata.pulse_count);
    params.Rmax = metadata.observation_max_range_m;
    params.Nv = static_cast<int>(metadata.velocity_oversampling);
    params.waveProcessType =
        static_cast<WaveProcessType>(metadata.wave_process_type);
    params.realtimeMode = false;
    params.updateDerivedParams();
    return params;
}

void compare_matrix(
    const std::vector<std::vector<cd>>& actual,
    const std::vector<std::vector<cd>>& expected,
    const char* name) {
    require(actual.size() == expected.size(), "matrix row count differs");
    for (std::size_t row = 0; row < actual.size(); ++row) {
        require(
            actual[row].size() == expected[row].size(),
            "matrix column count differs");
        for (std::size_t column = 0;
             column < actual[row].size();
             ++column) {
            if (actual[row][column] != expected[row][column]) {
                std::cerr << name << " differs at " << row << ','
                          << column << '\n';
                throw std::runtime_error("original algorithm output differs");
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: cpi-adapter-test <CPI-directory>\n";
            return 2;
        }
        const auto cpi = radar_example::load_cpi(argv[1]);
        std::vector<uestcradar::ComplexInt16> samples(
            cpi.metadata.samples_per_channel);
        std::memcpy(samples.data(), cpi.cs16.data(), cpi.cs16.size());
        const radar_algorithm::AdaptedCpi adapted =
            radar_algorithm::adapt_complete_cpi(
                cpi.metadata,
                {samples.data(), 1, cpi.metadata.samples_per_channel});
        const EchoProcessInput reference_input = make_reference_input(cpi);
        const RadarParams reference_params =
            make_reference_params(cpi.metadata);
        require(
            adapted.input.echo == reference_input.echo &&
                adapted.input.pulseTime == reference_input.pulseTime &&
                adapted.input.pulsePhase == reference_input.pulsePhase &&
                adapted.input.pulseFreq == reference_input.pulseFreq &&
                adapted.input.wd0 == reference_input.wd0,
            "IQ adapter changed dequantized input or pulse parameters");

        bool nonuniform = false;
        const auto even_stride = cpi.metadata.samples_per_channel /
            cpi.metadata.pulse_count;
        for (std::size_t pulse = 1;
             pulse < cpi.metadata.pulse_count;
             ++pulse) {
            const auto start = static_cast<std::uint64_t>(std::llround(
                adapted.input.pulseTime[pulse] * adapted.params.fs));
            if (start != pulse * even_stride) {
                nonuniform = true;
                break;
            }
        }
        require(
            nonuniform,
            "test fixture does not prove pulse-time window semantics");

        const EchoProcessOutput actual =
            radar_algorithm::run_original_algorithm(adapted);
        require(
            actual.pulseCompressed.size() == 64 &&
                !actual.pulseCompressed.empty() &&
                24 + actual.pulseCompressed.front().size() *
                        sizeof(uestcradar::ComplexFloat32) <=
                    4194304,
            "pulse output does not fit the configured Sidecar payload");
        const EchoProcessOutput expected =
            processEcho(reference_input, reference_params);
        compare_matrix(
            actual.pulseCompressed,
            expected.pulseCompressed,
            "pulseCompressed");
        compare_matrix(actual.RDMap, expected.RDMap, "RDMap");
        require(
            actual.rangeAxis == expected.rangeAxis &&
                actual.velocityAxis == expected.velocityAxis,
            "original algorithm axes differ");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cpi-adapter-test: " << error.what() << '\n';
        return 1;
    }
}
