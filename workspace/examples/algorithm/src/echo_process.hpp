#ifndef ECHO_PROCESS_HPP
#define ECHO_PROCESS_HPP

#include <vector>
#include <complex>
#include "radar_params.hpp"

using cd = std::complex<double>;

struct EchoProcessInput {
    std::vector<cd> echo;

    // 对应 MATLAB 中 R_t
    std::vector<double> pulseTime;

    // 对应 MATLAB 中 fai 或 angle(s_opt)
    std::vector<double> pulsePhase;

    // 对应 MATLAB 中 R_f，注意这里是实际载频，不是 R_f - f0
    std::vector<double> pulseFreq;

    // 对应 MATLAB 中 wd0
    std::vector<double> wd0;
};

struct EchoProcessOutput {
    std::vector<std::vector<cd>> pulseCompressed;
    std::vector<std::vector<cd>> RDMap;

    std::vector<double> rangeAxis;
    std::vector<double> velocityAxis;

    double peakValueDb = 0.0;
    double peakRange = 0.0;
    double peakVelocity = 0.0;

    int peakRangeIndex = 0;
    int peakVelocityIndex = 0;

    double preIntegrationSnrDb = 0.0;
    double postIntegrationSnrDb = 0.0;
    double integrationSnrGainDb = 0.0;
    double idealIntegrationSnrGainDb = 0.0;
    double idealAmplitudeGainDb = 0.0;
};

EchoProcessOutput processEcho(
    const EchoProcessInput& input,
    const RadarParams& params
);

#endif

