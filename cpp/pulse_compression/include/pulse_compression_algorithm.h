#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cstdint>
#include <stdexcept>
#include <cmath>

namespace pulse_compression_data = cycore::algorithm::pulse_compression;

struct ComplexReplica {
    double cos_val;
    double sin_val;
};

class PulseCompressionAlgorithm {
public:
    explicit PulseCompressionAlgorithm(const cycore::sdk::Params& params)
        : num_channels_(ReadSizeParam(params, "num_channels", pulse_compression_data::kDefaultNumChannels)),
          num_pulses_(ReadSizeParam(params, "num_pulses", pulse_compression_data::kDefaultNumPulses)),
          samples_per_pulse_(ReadSizeParam(params, "samples_per_pulse", pulse_compression_data::kDefaultSamplesPerPulse)) {
        if (num_channels_ == 0 || num_pulses_ == 0 || samples_per_pulse_ == 0) {
            throw std::invalid_argument("Invalid pulse compression dimensions");
        }

        // 🟢 构造预存的 256 点时域发射 Chirp Replica，消除 work 中的三角函数 CPU 运行开销
        ref_replica_.resize(256);
        for (std::size_t m = 0; m < 256; ++m) {
            double t = static_cast<double>(m) / 30.72e6;
            // K = B / Tp = 20e6 / (256 / 30.72e6) = 2.4e12
            // f_start = -10e6
            double ref_phase = (2.0 * 3.14159265358979323846) * (-10e6) * t + 3.14159265358979323846 * 2.4e12 * t * t;
            ref_replica_[m] = ComplexReplica{ std::cos(ref_phase), std::sin(ref_phase) };
        }
    }

    bool work(cycore::sdk::Reader<pulse_compression_data::InputSample>& in,
              cycore::sdk::Writer<pulse_compression_data::OutputSample>& out) {
        auto input = in.read_cube(num_channels_, num_pulses_, samples_per_pulse_);
        if (!input) {
            return false;
        }
        auto output = out.reserve_cube(num_channels_, num_pulses_, samples_per_pulse_);
        if (!output) {
            return false;
        }

        // 🟢 执行多通道并行时域滑动互相关匹配滤波
        for (std::size_t pulse = 0; pulse < num_pulses_; ++pulse) {
            for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
                for (std::size_t channel = 0; channel < num_channels_; ++channel) {
                    double sum_i = 0.0;
                    double sum_q = 0.0;
                    // 滑动互相关：使用 256 点发射 Chirp Replica
                    for (std::size_t m = 0; m < 256; ++m) {
                        std::size_t idx = sample + m;
                        if (idx < samples_per_pulse_) {
                            // 🟢 精准获取当前通道的时域样点
                            auto x = (*input)(channel, pulse, idx);
                            // 直接读取预存的参考信号，不再运行 std::cos/std::sin
                            double ref_cos = ref_replica_[m].cos_val;
                            double ref_sin = ref_replica_[m].sin_val;

                            // 复数共轭乘法: x * conj(ref)
                            sum_i += static_cast<double>(x.i) * ref_cos + static_cast<double>(x.q) * ref_sin;
                            sum_q += static_cast<double>(x.q) * ref_cos - static_cast<double>(x.i) * ref_sin;
                        }
                    }
                    // 计算时域互相关幅值包络并除以相干积累增益
                    double amp = std::sqrt(sum_i * sum_i + sum_q * sum_q);
                    std::int16_t out_val = static_cast<std::int16_t>(std::round(amp / 256.0));

                    (*output)(channel, pulse, sample) = pulse_compression_data::OutputSample{
                        out_val, // 实部输出当前通道的时域脉压包络
                        0
                    };
                }
            }
        }
        return true;
    }

private:
    static std::size_t ReadSizeParam(const cycore::sdk::Params& params,
                                     const std::string& key,
                                     std::size_t fallback) {
        const auto value = params.get<std::int64_t>(key, static_cast<std::int64_t>(fallback));
        if (value <= 0) {
            throw std::invalid_argument(key + " must be positive");
        }
        return static_cast<std::size_t>(value);
    }

    std::size_t num_channels_;
    std::size_t num_pulses_;
    std::size_t samples_per_pulse_;
    std::vector<ComplexReplica> ref_replica_; // 预存的本地时域参考信号
};
