#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cfar_plotter_data = cycore::algorithm::cfar_plotter;

class CfarPlotterAlgorithm {
public:
    explicit CfarPlotterAlgorithm(const cycore::sdk::Params& params)
        : num_channels_(ReadSizeParam(params, "num_channels", cfar_plotter_data::kDefaultNumChannels)),
          num_pulses_(ReadSizeParam(params, "num_pulses", cfar_plotter_data::kDefaultNumPulses)),
          samples_per_pulse_(ReadSizeParam(params, "samples_per_pulse", cfar_plotter_data::kDefaultSamplesPerPulse)),
          threshold_(params.get<double>("threshold", cfar_plotter_data::kDefaultThreshold)) {}

    bool work(cycore::sdk::Reader<cfar_plotter_data::InputSample>& in,
              cycore::sdk::Writer<cfar_plotter_data::OutputSample>& out) {
        auto input = in.read_cube(num_channels_, num_pulses_, samples_per_pulse_);
        if (!input) {
            return false;
        }

        std::size_t plot_count = 0;
        for (std::size_t pulse = 0; pulse < num_pulses_; ++pulse) {
            for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
                for (std::size_t channel = 0; channel < num_channels_; ++channel) {
                    if ((*input)(channel, pulse, sample) > static_cast<float>(threshold_)) {
                        ++plot_count;
                    }
                }
            }
        }

        auto plots = out.reserve_raw_array<cfar_plotter_data::Plot>(plot_count);
        if (!plots) {
            return false;
        }

        std::size_t index = 0;
        for (std::size_t pulse = 0; pulse < num_pulses_; ++pulse) {
            for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
                for (std::size_t channel = 0; channel < num_channels_; ++channel) {
                    const float power = (*input)(channel, pulse, sample);
                    if (power <= static_cast<float>(threshold_)) {
                        continue;
                    }
                    (*plots)[index++] = cfar_plotter_data::Plot{
                        static_cast<std::uint32_t>(channel),
                        static_cast<std::uint32_t>(pulse),
                        static_cast<std::uint32_t>(sample),
                        power,
                        static_cast<float>(sample),
                        static_cast<float>(pulse)};
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
    double threshold_;
};
