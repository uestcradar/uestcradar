#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace range_doppler_data = cycore::algorithm::range_doppler;

class RangeDopplerAlgorithm {
public:
    explicit RangeDopplerAlgorithm(const cycore::sdk::Params& params)
        : num_channels_(ReadSizeParam(params, "num_channels", range_doppler_data::kDefaultNumChannels)),
          num_pulses_(ReadSizeParam(params, "num_pulses", range_doppler_data::kDefaultNumPulses)),
          samples_per_pulse_(ReadSizeParam(params, "samples_per_pulse", range_doppler_data::kDefaultSamplesPerPulse)) {}

    bool work(cycore::sdk::Reader<range_doppler_data::InputSample>& in,
              cycore::sdk::Writer<range_doppler_data::OutputSample>& out) {
        auto input = in.read_cube(num_channels_, num_pulses_, samples_per_pulse_);
        if (!input) {
            return false;
        }
        // 🟢 输出通道数固定为 1
        auto output = out.reserve_cube(1, num_pulses_, samples_per_pulse_);
        if (!output) {
            return false;
        }

        // 🟢 仅将输出的通道 0 置零
        for (std::size_t pulse = 0; pulse < num_pulses_; ++pulse) {
            for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
                (*output)(0, pulse, sample) = 0.0f;
            }
        }

        if (num_pulses_ < 2 || samples_per_pulse_ < 2) {
            return true;
        }

        // 🟢 提取输入通道 0 进行目标相位估计并写入唯一的输出通道 0 中
        const auto base = (*input)(0, 0, 0);
        const auto next_sample = (*input)(0, 0, 1);
        const auto next_pulse = (*input)(0, 1, 0);
        const std::size_t range_bin = EstimateBin(base, next_sample, samples_per_pulse_);
        const std::size_t doppler_bin = EstimateBin(base, next_pulse, num_pulses_);
        (*output)(0, doppler_bin, range_bin) = Power(base);

        static std::uint32_t print_cnt = 0;
        if (++print_cnt % 30 == 0) {
            std::printf("[RangeDoppler] Ch0 Target estimated at: doppler=%zu, range=%zu, power=%.2f\n",
                        doppler_bin, range_bin, Power(base));
            std::fflush(stdout);
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

    static std::size_t EstimateBin(const cy::common::CS16& current,
                                   const cy::common::CS16& next,
                                   std::size_t bin_count) {
        const double dot = static_cast<double>(current.i) * static_cast<double>(next.i) +
                           static_cast<double>(current.q) * static_cast<double>(next.q);
        const double cross = static_cast<double>(current.i) * static_cast<double>(next.q) -
                             static_cast<double>(current.q) * static_cast<double>(next.i);
        double phase = std::atan2(cross, dot);
        if (phase < 0.0) {
            phase += 2.0 * kPi;
        }
        const auto rounded = static_cast<std::size_t>(
            std::llround((phase / (2.0 * kPi)) * static_cast<double>(bin_count)));
        return rounded % bin_count;
    }

    static float Power(const cy::common::CS16& sample) {
        const float i = static_cast<float>(sample.i);
        const float q = static_cast<float>(sample.q);
        return i * i + q * q;
    }

    static constexpr double kPi = 3.14159265358979323846;

    std::size_t num_channels_;
    std::size_t num_pulses_;
    std::size_t samples_per_pulse_;
};
