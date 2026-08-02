#include "echo_process.hpp"

#include <iostream>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <numeric>

#ifdef AGILE_SIGNAL_USE_OPENMP
#include <omp.h>
#endif

namespace {
const double PI = std::acos(-1.0);
const cd J(0.0, 1.0);

std::vector<double> linspace(double a, double b, int N) {
    std::vector<double> x(N);

    if (N <= 0) {
        return x;
    }

    if (N == 1) {
        x[0] = a;
        return x;
    }

    double step = (b - a) / static_cast<double>(N - 1);

    for (int i = 0; i < N; ++i) {
        x[i] = a + i * step;
    }

    return x;
}

double hammingWindow(int n, int N) {
    if (N <= 1) {
        return 1.0;
    }

    return 0.54 - 0.46 * std::cos(2.0 * PI * n / static_cast<double>(N - 1));
}

double parabolicOffset(double left, double center, double right) {
    double denom = left - 2.0 * center + right;

    if (std::abs(denom) < 1e-18) {
        return 0.0;
    }

    double offset = 0.5 * (left - right) / denom;
    return std::max(-0.5, std::min(0.5, offset));
}

double medianValue(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }

    size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    double med = values[mid];

    if (values.size() % 2 == 0) {
        std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
        med = 0.5 * (med + values[mid - 1]);
    }

    return med;
}

int nextPow2(int n) {
    int p = 1;
    while (p < n) {
        p <<= 1;
    }
    return p;
}

void fftInPlace(std::vector<cd>& a, bool inverse) {
    int n = static_cast<int>(a.size());

    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;

        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * PI / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        cd wlen = std::exp(J * ang);

        for (int i = 0; i < n; i += len) {
            cd w = 1.0;
            for (int j = 0; j < len / 2; ++j) {
                cd u = a[i + j];
                cd v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (cd& x : a) {
            x /= static_cast<double>(n);
        }
    }
}

std::vector<cd> convFullFft(
    const std::vector<cd>& x,
    const std::vector<cd>& h
) {
    int Nx = static_cast<int>(x.size());
    int Nh = static_cast<int>(h.size());
    int Ny = Nx + Nh - 1;
    int Nfft = nextPow2(Ny);

    std::vector<cd> X(Nfft, cd(0.0, 0.0));
    std::vector<cd> H(Nfft, cd(0.0, 0.0));

    std::copy(x.begin(), x.end(), X.begin());
    std::copy(h.begin(), h.end(), H.begin());

    fftInPlace(X, false);
    fftInPlace(H, false);

    for (int i = 0; i < Nfft; ++i) {
        X[i] *= H[i];
    }

    fftInPlace(X, true);
    X.resize(Ny);
    return X;
}

std::vector<double> generateRangeAxis(double R0, const RadarParams& params, int N) {
    std::vector<double> rangeAxis(N);

    for (int n = 0; n < N; ++n) {
        rangeAxis[n] = n * params.c * params.dt / 2.0;
    }

    return rangeAxis;
}

std::vector<cd> convFull(
    const std::vector<cd>& x,
    const std::vector<cd>& h
) {
    int Nx = static_cast<int>(x.size());
    int Nh = static_cast<int>(h.size());
    int Ny = Nx + Nh - 1;

    if (Nx <= 0 || Nh <= 0) {
        return {};
    }

    if (static_cast<long long>(Nx) * static_cast<long long>(Nh) > 2000000LL) {
        return convFullFft(x, h);
    }

    std::vector<cd> y(Ny, cd(0.0, 0.0));

#ifdef AGILE_SIGNAL_USE_OPENMP
    #pragma omp parallel for schedule(static) if (Ny > 4096)
#endif
    for (int n = 0; n < Ny; ++n) {
        cd sumVal = 0.0;

        int kMin = std::max(0, n - Nh + 1);
        int kMax = std::min(n, Nx - 1);

        for (int k = kMin; k <= kMax; ++k) {
            sumVal += x[k] * h[n - k];
        }

        y[n] = sumVal;
    }

    return y;
}

// 根据雷达参数构造基带 LFM 匹配滤波器。
// 对应发射基带信号：s(t)=exp(j*pi*K*t^2)，K=B/tao。
// 匹配滤波器为：h[n] = conj(s[N_s-1-n])。
std::vector<cd> generateLfmMatchedFilterFromParams(const RadarParams& params) {
    int Ns = params.N_s;

    if (Ns <= 0) {
        Ns = static_cast<int>(std::round(params.tao * params.fs));
    }

    if (Ns <= 0) {
        throw std::runtime_error("failed to generate matched filter: N_s <= 0.");
    }

    if (params.fs <= 0.0) {
        throw std::runtime_error("failed to generate matched filter: fs <= 0.");
    }

    if (params.tao <= 0.0) {
        throw std::runtime_error("failed to generate matched filter: tao <= 0.");
    }

    double K = params.B / params.tao;

    // Match the MATLAB reference waveform time origin. A non-centered chirp
    // shifts the compressed peak by tao/2, which is 6.75 km for the defaults.
    const bool useCenteredTime = true;

    std::vector<cd> s(Ns, cd(0.0, 0.0));

    for (int n = 0; n < Ns; ++n) {
        double t = 0.0;

        if (useCenteredTime) {
            t = (static_cast<double>(n) - static_cast<double>(Ns - 1) / 2.0) / params.fs;
        } else {
            t = static_cast<double>(n) / params.fs;
        }

        double phase = PI * K * t * t;
        s[n] = std::exp(J * phase);
    }

    std::vector<cd> h(Ns, cd(0.0, 0.0));

    for (int n = 0; n < Ns; ++n) {
        h[n] = std::conj(s[Ns - 1 - n]) * hammingWindow(n, Ns);
    }

    return h;
}

// 对应 MATLAB:
// F = exp(-1j*2*pi*(2*vl/c).*R_f.*R_t);
// Ymtd1 = (wd0.*F).' * Yt
std::vector<std::vector<cd>> coherentIntegrateNUDFT(
    const std::vector<std::vector<cd>>& Yt,//脉压输入结果
    const EchoProcessInput& input,//输入的结构体，包含频率、相位、时序等
    const RadarParams& params,//雷达参数
    const std::vector<double>& velocityAxis//速度轴
) {
    int pulseN = params.pulseN;
    int ND = static_cast<int>(velocityAxis.size());
    int N = static_cast<int>(Yt[0].size());

    std::vector<std::vector<cd>> RDMap(//定义了一个二维容器，同时也是标准的二维容器定义方法
        ND,
        std::vector<cd>(N, cd(0.0, 0.0))//一个长度为N，初始值为0的复数容器
    );

#ifdef AGILE_SIGNAL_USE_OPENMP
    #pragma omp parallel for schedule(static) if (ND > 2)
#endif
    for (int k = 0; k < ND; ++k) {
        double v = velocityAxis[k];

        for (int m = 0; m < pulseN; ++m) {
            double Rf = input.pulseFreq[m];
            double Rt = input.pulseTime[m];
            double w = input.wd0[m];

            cd F = std::exp(
                -J * 2.0 * PI * (2.0 * v / params.c) * Rf * Rt
            );

            cd weight = w * F;

            for (int n = 0; n < N; ++n) {
                RDMap[k][n] += weight * Yt[m][n];
            }
        }
    }

    return RDMap;
}

void findRDPeak(EchoProcessOutput& output) {
    if (output.RDMap.empty() || output.RDMap[0].empty()) {
        return;
    }

    double maxVal = -1e300;
    int maxK = 0;
    int maxR = 0;

    int K = static_cast<int>(output.RDMap.size());
    int R = static_cast<int>(output.RDMap[0].size());

    for (int k = 0; k < K; ++k) {
        for (int r = 0; r < R; ++r) {
            double valDb = 20.0 * std::log10(std::abs(output.RDMap[k][r]) + 1e-12);

            if (valDb > maxVal) {
                maxVal = valDb;
                maxK = k;
                maxR = r;
            }
        }
    }

    output.peakValueDb = maxVal;
    output.peakVelocityIndex = maxK;
    output.peakRangeIndex = maxR;

    if (maxR < static_cast<int>(output.rangeAxis.size())) {
        output.peakRange = output.rangeAxis[maxR];

        if (maxR > 0 && maxR + 1 < R && maxR + 1 < static_cast<int>(output.rangeAxis.size())) {
            double left = 20.0 * std::log10(std::abs(output.RDMap[maxK][maxR - 1]) + 1e-12);
            double center = 20.0 * std::log10(std::abs(output.RDMap[maxK][maxR]) + 1e-12);
            double right = 20.0 * std::log10(std::abs(output.RDMap[maxK][maxR + 1]) + 1e-12);
            double step = output.rangeAxis[maxR + 1] - output.rangeAxis[maxR];
            output.peakRange += parabolicOffset(left, center, right) * step;
        }
    }

    if (maxK < static_cast<int>(output.velocityAxis.size())) {
        output.peakVelocity = output.velocityAxis[maxK];

        if (maxK > 0 && maxK + 1 < K && maxK + 1 < static_cast<int>(output.velocityAxis.size())) {
            double left = 20.0 * std::log10(std::abs(output.RDMap[maxK - 1][maxR]) + 1e-12);
            double center = 20.0 * std::log10(std::abs(output.RDMap[maxK][maxR]) + 1e-12);
            double right = 20.0 * std::log10(std::abs(output.RDMap[maxK + 1][maxR]) + 1e-12);
            double step = output.velocityAxis[maxK + 1] - output.velocityAxis[maxK];
            output.peakVelocity += parabolicOffset(left, center, right) * step;
        }
    }
}

void estimateIntegrationGain(
    EchoProcessOutput& output,
    const EchoProcessInput& input,
    const RadarParams& params
) {
    if (!params.enableSnrDiagnostics) {
        return;
    }

    if (output.pulseCompressed.empty() || output.RDMap.empty()) {
        return;
    }

    int pulseN = static_cast<int>(output.pulseCompressed.size());
    int rangeSize = static_cast<int>(output.pulseCompressed[0].size());
    int dopplerSize = static_cast<int>(output.RDMap.size());
    int peakR = output.peakRangeIndex;
    int peakK = output.peakVelocityIndex;

    if (peakR < 0 || peakR >= rangeSize || peakK < 0 || peakK >= dopplerSize) {
        return;
    }

    int rangeGuard = std::max(params.N_s / 2, 128);
    int dopplerGuard = 2;

    double preSignalPower = 0.0;
    for (int m = 0; m < pulseN; ++m) {
        preSignalPower += std::norm(output.pulseCompressed[m][peakR]);
    }
    preSignalPower /= std::max(1, pulseN);

    std::vector<double> preNoisePowers;
    preNoisePowers.reserve(static_cast<size_t>(pulseN) * static_cast<size_t>(rangeSize / 8));

    int noiseStride = std::max(1, rangeSize / 20000);
    for (int m = 0; m < pulseN; ++m) {
        for (int r = 0; r < rangeSize; r += noiseStride) {
            if (std::abs(r - peakR) <= rangeGuard) {
                continue;
            }
            preNoisePowers.push_back(std::norm(output.pulseCompressed[m][r]));
        }
    }

    std::vector<double> postNoisePowers;
    postNoisePowers.reserve(static_cast<size_t>(dopplerSize) * static_cast<size_t>(rangeSize / 8));

    for (int k = 0; k < dopplerSize; ++k) {
        for (int r = 0; r < rangeSize; r += noiseStride) {
            if (std::abs(r - peakR) <= rangeGuard && std::abs(k - peakK) <= dopplerGuard) {
                continue;
            }
            postNoisePowers.push_back(std::norm(output.RDMap[k][r]));
        }
    }

    double preNoisePower = medianValue(preNoisePowers);
    double postNoisePower = medianValue(postNoisePowers);
    double postSignalPower = std::norm(output.RDMap[peakK][peakR]);

    const double eps = 1e-30;
    output.preIntegrationSnrDb = 10.0 * std::log10((preSignalPower + eps) / (preNoisePower + eps));
    output.postIntegrationSnrDb = 10.0 * std::log10((postSignalPower + eps) / (postNoisePower + eps));
    output.integrationSnrGainDb = output.postIntegrationSnrDb - output.preIntegrationSnrDb;

    double sumAbsW = 0.0;
    double sumAbsW2 = 0.0;
    for (double w : input.wd0) {
        double aw = std::abs(w);
        sumAbsW += aw;
        sumAbsW2 += aw * aw;
    }

    if (sumAbsW <= 0.0 || sumAbsW2 <= 0.0) {
        sumAbsW = static_cast<double>(pulseN);
        sumAbsW2 = static_cast<double>(pulseN);
    }

    output.idealAmplitudeGainDb = 20.0 * std::log10(sumAbsW + eps);
    output.idealIntegrationSnrGainDb = 10.0 * std::log10((sumAbsW * sumAbsW + eps) / (sumAbsW2 + eps));
}

void validateCommonInput(
    const EchoProcessInput& input,
    const RadarParams& params
) {
    if (input.echo.empty()) {
        throw std::runtime_error("input.echo is empty.");
    }

    if (input.pulseTime.size() != static_cast<size_t>(params.pulseN)) {
        throw std::runtime_error("pulseTime size does not match pulseN.");
    }

    if (input.pulseFreq.size() != static_cast<size_t>(params.pulseN)) {
        throw std::runtime_error("pulseFreq size does not match pulseN.");
    }

    if (input.wd0.size() != static_cast<size_t>(params.pulseN)) {
        throw std::runtime_error("wd0 size does not match pulseN.");
    }
}

// agile_s_process.m 和 agile_PRT_process.m 的公共部分：
// 整段回波先脉压，然后 PC(N_s:end)，再按 R_t 切片
std::vector<std::vector<cd>> wholeEchoPulseCompressionThenSlice(
    const EchoProcessInput& input,
    const RadarParams& params,
    int N,
    std::vector<cd> matchedFilter
) {
    std::vector<cd> PC = convFull(input.echo, matchedFilter);

    std::vector<cd> ytPC;

    int start = params.N_s - 1;
    for (int i = start; i < static_cast<int>(PC.size()); ++i) {
        ytPC.push_back(PC[i]);
    }

    int N_CPI_afterPC = static_cast<int>(ytPC.size());

    ytPC.resize(ytPC.size() + N, cd(0.0, 0.0));

    std::vector<std::vector<cd>> Yt(
        params.pulseN,
        std::vector<cd>(N, cd(0.0, 0.0))
    );

#ifdef AGILE_SIGNAL_USE_OPENMP
    #pragma omp parallel for schedule(static) if (params.pulseN > 1)
#endif
    for (int m = 0; m < params.pulseN; ++m) {
        int Nt = static_cast<int>(input.pulseTime[m] * params.fs + 0.5);

        for (int n = 0; n < N; ++n) {
            int idx = Nt + n;

            if (idx >= 0 && idx < static_cast<int>(ytPC.size())) {
                Yt[m][n] = ytPC[idx];
            }
        }
    }

    (void)N_CPI_afterPC;
    return Yt;
}

EchoProcessOutput processEchoAgilePRT(
    const EchoProcessInput& input,
    const RadarParams& params
) {
    validateCommonInput(input, params);

    EchoProcessOutput output;

    double R0 = params.getObservationRangeMax();
    double v0 = params.getVelocityMax();
    int ND = params.getDopplerBins();
    int N = params.getObservationRangeSamples();

    output.rangeAxis = generateRangeAxis(R0, params, N);
    output.velocityAxis = linspace(-v0, v0, ND);
    std::vector<cd> matchedFilter = generateLfmMatchedFilterFromParams(params);

    output.pulseCompressed = wholeEchoPulseCompressionThenSlice(
        input,
        params,
        N,
        matchedFilter
    );

    output.RDMap = coherentIntegrateNUDFT(
        output.pulseCompressed,
        input,
        params,
        output.velocityAxis
    );

    findRDPeak(output);
    estimateIntegrationGain(output, input, params);

    return output;
}

EchoProcessOutput processEchoAgileS(
    const EchoProcessInput& input,
    const RadarParams& params
) {
    validateCommonInput(input, params);

    if (input.pulsePhase.size() != static_cast<size_t>(params.pulseN)) {
        throw std::runtime_error("pulsePhase size does not match pulseN.");
    }

    EchoProcessOutput output;

    double R0 = params.getObservationRangeMax();
    double v0 = params.getVelocityMax();
    int ND = params.getDopplerBins();
    int N = params.getObservationRangeSamples();

    output.rangeAxis = generateRangeAxis(R0, params, N);
    output.velocityAxis = linspace(-v0, v0, ND);
    std::vector<cd> matchedFilter = generateLfmMatchedFilterFromParams(params);

    output.pulseCompressed = wholeEchoPulseCompressionThenSlice(
        input,
        params,
        N,
        matchedFilter
    );

    // 对应 MATLAB:
    // Yt1 = Y_PC1 .* conj(s_opt)
    // 若 pulsePhase = angle(s_opt)，则 conj(s_opt)=exp(-j*pulsePhase)
#ifdef AGILE_SIGNAL_USE_OPENMP
    #pragma omp parallel for schedule(static) if (params.pulseN > 1)
#endif
    for (int m = 0; m < params.pulseN; ++m) {
        cd phaseComp = std::exp(-J * input.pulsePhase[m]);

        for (int n = 0; n < N; ++n) {
            output.pulseCompressed[m][n] *= phaseComp;
        }
    }

    output.RDMap = coherentIntegrateNUDFT(
        output.pulseCompressed,
        input,
        params,
        output.velocityAxis
    );

    findRDPeak(output);
    estimateIntegrationGain(output, input, params);

    return output;
}

EchoProcessOutput processEchoAgileF(
    const EchoProcessInput& input,
    const RadarParams& params
) {
    validateCommonInput(input, params);

    EchoProcessOutput output;

    double R0 = params.getObservationRangeMax();
    double v0 = params.getVelocityMax();
    int ND = params.getDopplerBins();
    int N = params.getObservationRangeSamples();

    output.rangeAxis = generateRangeAxis(R0, params, N);
    output.velocityAxis = linspace(-v0, v0, ND);
    std::vector<cd> matchedFilter = generateLfmMatchedFilterFromParams(params);

    // 对应 MATLAB:
    // echom_all1 = [echom_all1, zeros(1,N)];
    std::vector<cd> echoPad = input.echo;
    echoPad.resize(echoPad.size() + N, cd(0.0, 0.0));

    // 1. 按 R_t 从原始回波中切片
    std::vector<std::vector<cd>> Yraw(
        params.pulseN,
        std::vector<cd>(N, cd(0.0, 0.0))
    );

#ifdef AGILE_SIGNAL_USE_OPENMP
    #pragma omp parallel for schedule(static) if (params.pulseN > 1)
#endif
    for (int m = 0; m < params.pulseN; ++m) {
        int Nt = static_cast<int>(input.pulseTime[m] * params.fs + 0.5);

        for (int n = 0; n < N; ++n) {
            int idx = Nt + n;

            if (idx >= 0 && idx < static_cast<int>(echoPad.size())) {
                Yraw[m][n] = echoPad[idx];
            }
        }
    }

    // 2. 每个脉冲频移补偿 + 匹配滤波
    output.pulseCompressed.assign(
        params.pulseN,
        std::vector<cd>(N, cd(0.0, 0.0))
    );

#ifdef AGILE_SIGNAL_USE_OPENMP
    #pragma omp parallel for schedule(static) if (params.pulseN > 1)
#endif
    for (int m = 0; m < params.pulseN; ++m) {
        double Rf = input.pulseFreq[m];
        double df = Rf - params.f0;

        std::vector<cd> temp(N);
        cd downPhase = 1.0;
        cd downStep = std::exp(-J * 2.0 * PI * df * params.dt);

        // MATLAB:
        // temp = Yt1(i,:) .* exp(-1j*2*pi*(R_f(i)-f0)*t)
        for (int n = 0; n < N; ++n) {
            temp[n] = Yraw[m][n] * downPhase;
            downPhase *= downStep;
        }

        // MATLAB:
        // PC = ifft(fft(temp,N+N_s-1).*hf)
        // 这里用 full 卷积代替 FFT 卷积
        std::vector<cd> PC = convFull(temp, matchedFilter);

        // MATLAB:
        // Y_PC1(i,:) = PC(N_s:end).*exp(1j*2*pi*R_f(i)*t)
        int start = params.N_s - 1;
        cd upPhase = 1.0;
        cd upStep = std::exp(J * 2.0 * PI * Rf * params.dt);

        for (int n = 0; n < N; ++n) {
            int idx = start + n;

            if (idx >= 0 && idx < static_cast<int>(PC.size())) {
                output.pulseCompressed[m][n] = PC[idx] * upPhase;
            }

            upPhase *= upStep;
        }
    }

    output.RDMap = coherentIntegrateNUDFT(
        output.pulseCompressed,
        input,
        params,
        output.velocityAxis
    );

    findRDPeak(output);
    estimateIntegrationGain(output, input, params);

    return output;
}

} // namespace

EchoProcessOutput processEcho(
    const EchoProcessInput& input,
    const RadarParams& params
) {
    EchoProcessInput work = input;
    
    /*std::vector<cd> matchedFilter = generateLfmMatchedFilterFromParams(params);
    if (matchedFilter.empty()) {
        matchedFilter = generateLfmMatchedFilterFromParams(params);
        std::cout << "Matched filter generated from waveform parameters, length = "
                  << matchedFilter.size() << std::endl;
    }*/

    switch (params.waveProcessType) {
    case WaveProcessType::RADON_S:
    case WaveProcessType::RADON_S_T:
        return processEchoAgileS(work, params);

    case WaveProcessType::RADON_PRT:
        return processEchoAgilePRT(work, params);

    case WaveProcessType::RADON_F:
    case WaveProcessType::RADON_F_T:
        return processEchoAgileF(work, params);

    case WaveProcessType::LFM:
    default:
        return processEchoAgilePRT(work, params);
    }
}

