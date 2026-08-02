#ifndef RADAR_PARAMS_HPP
#define RADAR_PARAMS_HPP

#include <cmath>
#include <iostream>
#include <string>

enum class WaveProcessType {
    LFM,
    RADON_S,
    RADON_PRT,
    RADON_F,
    RADON_F_T,
    RADON_S_T
};

inline std::string waveProcessTypeToString(WaveProcessType type) {
    switch (type) {
    case WaveProcessType::LFM:       return "LFM";
    case WaveProcessType::RADON_S:   return "RADON_S";
    case WaveProcessType::RADON_PRT: return "RADON_PRT";
    case WaveProcessType::RADON_F:   return "RADON_F";
    case WaveProcessType::RADON_F_T: return "RADON_F_T";
    case WaveProcessType::RADON_S_T: return "RADON_S_T";
    default:                         return "UNKNOWN";
    }
}

struct RadarParams {
    double c = 3.0e8;
    double f0 = 3.0e9;
    double lambda = c / f0;

    double B = 2.0e6;
    double tao = 90.0e-6;
    double PRT = 361.25e-6;
    double fs = 30.72e6;
    double dt = 1.0 / fs;

    int pulseN = 64;
    int N_fft = 1024;

    int N_s = 0;
    int N_PRT = 0;
    int N_CPI = 0;

    double Rmax = 200.0e3;
    double R_u = 0.0;

    double Rt = 0.0;
    double vt = 0.0;

    int Nmax = 0;
    int N_echo = 0;

    int Nv = 2;

    WaveProcessType waveProcessType = WaveProcessType::LFM;

    bool realtimeMode = false;
    bool enableSnrDiagnostics = true;

    RadarParams() {
        updateDerivedParams();
    }

    void updateDerivedParams() {
        dt = 1.0 / fs;
        lambda = c / f0;

        N_s = static_cast<int>(tao * fs + 0.5);
        N_PRT = static_cast<int>(PRT * fs + 0.5);
        N_CPI = pulseN * N_PRT;

        R_u = c * PRT / 2.0;
        Nmax = static_cast<int>(Rmax / (c * dt / 2.0) + 0.5);
        N_echo = N_CPI + Nmax;
    }

    int getObservationRangeSamples() const {
        int Nr = static_cast<int>(std::floor(Rmax / R_u));
        double R0 = c * PRT / 2.0 * Nr;
        double tWait = 2.0 * R0 / c;
        return static_cast<int>(tWait * fs + 0.5);
    }

    double getObservationRangeMax() const {
        int Nr = static_cast<int>(std::floor(Rmax / R_u));
        return c * PRT / 2.0 * Nr;
    }

    double getVelocityMax() const {
        return lambda / 4.0 / PRT * Nv;
    }

    int getDopplerBins() const {
        return pulseN * Nv + 1;
    }

    void print() const {
        std::cout << "========================================\n";
        std::cout << "Radar Parameters\n";
        std::cout << "========================================\n";
        std::cout << "wave type = " << waveProcessTypeToString(waveProcessType) << "\n";
        std::cout << "c         = " << c << " m/s\n";
        std::cout << "f0        = " << f0 << " Hz\n";
        std::cout << "lambda    = " << lambda << " m\n";
        std::cout << "B         = " << B << " Hz\n";
        std::cout << "tao       = " << tao << " s\n";
        std::cout << "PRT       = " << PRT << " s\n";
        std::cout << "fs        = " << fs << " Hz\n";
        std::cout << "dt        = " << dt << " s\n";
        std::cout << "pulseN    = " << pulseN << "\n";
        std::cout << "N_s       = " << N_s << "\n";
        std::cout << "N_PRT     = " << N_PRT << "\n";
        std::cout << "N_CPI     = " << N_CPI << "\n";
        std::cout << "R_u       = " << R_u << " m\n";
        std::cout << "Rmax      = " << Rmax << " m\n";
        std::cout << "N_obs     = " << getObservationRangeSamples() << "\n";
        std::cout << "N_D       = " << getDopplerBins() << "\n";
        std::cout << "realtime  = " << (realtimeMode ? "true" : "false") << "\n";
        std::cout << "snr diag  = " << (enableSnrDiagnostics ? "true" : "false") << "\n";
        std::cout << "========================================\n";
    }
};

#endif

