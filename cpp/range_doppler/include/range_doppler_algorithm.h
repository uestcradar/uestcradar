#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace rd_data = cycore::algorithm::range_doppler;

class RangeDopplerAlgorithm {
public:
    explicit RangeDopplerAlgorithm(const cycore::sdk::Params& params)
        : num_channels_(ReadSizeParam(params, "num_channels", rd_data::kDefaultNumChannels)),
          num_pulses_(ReadSizeParam(params, "num_pulses", rd_data::kDefaultNumPulses)),
          samples_per_pulse_(ReadSizeParam(params, "samples_per_pulse", rd_data::kDefaultSamplesPerPulse)) {
        
        if (num_channels_ == 0 || num_pulses_ == 0 || samples_per_pulse_ == 0) {
            throw std::invalid_argument("Invalid Range-Doppler dimensions");
        }

        // 预计算慢时间维 (num_pulses) DFT 旋转因子以消除实时三角函数开销
        twiddle_factors_.resize(num_pulses_);
        const double pi = 3.14159265358979323846;
        for (std::size_t m = 0; m < num_pulses_; ++m) {
            double angle = 2.0 * pi * m / num_pulses_;
            twiddle_factors_[m].cos_val = static_cast<float>(std::cos(angle));
            twiddle_factors_[m].sin_val = static_cast<float>(std::sin(angle));
        }
    }

    bool work(cycore::sdk::Reader<rd_data::InputSample>& in,
              cycore::sdk::Writer<rd_data::OutputSample>& out) {
        auto input = in.read_cube(num_channels_, num_pulses_, samples_per_pulse_);
        if (!input) {
            return false;
        }



        auto output = out.reserve_cube(num_channels_, num_pulses_, samples_per_pulse_);
        if (!output) {
            return false;
        }

        const float scale = 1.0f / std::sqrt(static_cast<float>(num_pulses_));
        rd_data::OutputSample* out_ptr = output->data();
        std::size_t cpi_size = num_pulses_ * samples_per_pulse_;

        for (std::size_t ch = 0; ch < num_channels_; ++ch) {
            for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
                for (std::size_t k = 0; k < num_pulses_; ++k) {
                    float sum_i = 0.0f;
                    float sum_q = 0.0f;

                    for (std::size_t n = 0; n < num_pulses_; ++n) {
                        auto x = (*input)(ch, n, sample);

                        std::size_t idx = (k * n) % num_pulses_;
                        const auto& twiddle = twiddle_factors_[idx];

                        sum_i += static_cast<float>(x.i) * twiddle.cos_val + static_cast<float>(x.q) * twiddle.sin_val;
                        sum_q += static_cast<float>(x.q) * twiddle.cos_val - static_cast<float>(x.i) * twiddle.sin_val;
                    }

                    float amp = std::sqrt(sum_i * sum_i + sum_q * sum_q);
                    float amp_scaled = amp * scale;
                    
                    // 🟢 恢复绝对对数分贝输出，保留微弱目标在超宽动态范围下的探测能力
                    std::size_t out_idx = ch * cpi_size + k * samples_per_pulse_ + sample;
                    out_ptr[out_idx] = 20.0f * std::log10(std::max(amp_scaled, 1e-6f));
                }
            }
        }

        // 🌟 新增：多普勒积累后 64 个 bin 能量分布 debug 诊断打印
        static int print_accum_count = 0;
        if (print_accum_count < 3 && samples_per_pulse_ > 37) {
            std::fprintf(stderr, "\n=== [DEBUG RD ACCUMULATION] Frame %d, Channel 0, Sample 37 ===\n", print_accum_count);
            for (std::size_t k = 0; k < num_pulses_; ++k) {
                std::size_t out_idx = 0 * cpi_size + k * samples_per_pulse_ + 37;
                std::fprintf(stderr, "  Doppler Bin %zu: Value = %.4f dB\n", k, out_ptr[out_idx]);
            }
            std::fprintf(stderr, "=== [DEBUG END] ===\n\n");
            print_accum_count++;
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

    struct TwiddleFactor {
        float cos_val;
        float sin_val;
    };

    std::size_t num_channels_;
    std::size_t num_pulses_;
    std::size_t samples_per_pulse_;
    std::vector<TwiddleFactor> twiddle_factors_;
};
