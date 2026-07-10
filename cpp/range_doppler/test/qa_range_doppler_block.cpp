#include "range_doppler_algorithm.h"

#include <cycore_algorithm_sdk.h>
#include <flowgraph/block.h>
#include <flowgraph/port.h>
#include <flowgraph/graph.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace fg = cy::flowgraph;
using InputSample = cycore::algorithm::range_doppler::InputSample;
using OutputSample = cycore::algorithm::range_doppler::OutputSample;

// 1. 静态测试仿真源 Block：直接注入含距离和多普勒调制的复单音信号
struct SimSource : public fg::Block<SimSource> {
    fg::PortOut<InputSample> out;
    CY_MAKE_REFLECTABLE(SimSource, out);

    std::size_t channel_count = 2;
    std::size_t pulses = 8;
    std::size_t samples_per_pulse = 4;
    double range_bin = 2.0;
    double doppler_bin = 3.0;
    double amplitude = 1000.0;

    void process_work() {
        std::size_t count = channel_count * pulses * samples_per_pulse;
        auto span = out.reserve(count);
        if (span.empty()) return;

        const double pi = 3.14159265358979323846;
        for (std::size_t p = 0; p < pulses; ++p) {
            for (std::size_t s = 0; s < samples_per_pulse; ++s) {
                for (std::size_t ch = 0; ch < channel_count; ++ch) {
                    double phase = 2.0 * pi * (
                        range_bin * static_cast<double>(s) / static_cast<double>(samples_per_pulse) +
                        doppler_bin * static_cast<double>(p) / static_cast<double>(pulses)
                    );
                    
                    std::size_t idx = ((p * samples_per_pulse + s) * channel_count) + ch;
                    span[idx] = InputSample{
                        static_cast<std::int16_t>(std::round(amplitude * std::cos(phase))),
                        static_cast<std::int16_t>(std::round(amplitude * std::sin(phase)))
                    };
                }
            }
        }
        span.commit(count);
    }
};

// 2. 静态测试校验 Sink Block：对 RD 图输出在 Epsilon 门限内执行数学断言
struct SimSink : public fg::Block<SimSink> {
    fg::PortIn<OutputSample> in;
    CY_MAKE_REFLECTABLE(SimSink, in);

    std::size_t channel_count = 2;
    std::size_t pulses = 8;
    std::size_t samples_per_pulse = 4;
    double range_bin = 2.0;
    double doppler_bin = 3.0;
    double amplitude = 1000.0;

    void process_work() {
        std::size_t count = channel_count * pulses * samples_per_pulse;
        auto span = in.get(count);
        if (span.empty()) return;

        double expected_peak = 20.0 * std::log10(amplitude * std::sqrt(static_cast<double>(pulses)));
        const double epsilon = 1.0;

        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            for (std::size_t s = 0; s < samples_per_pulse; ++s) {
                for (std::size_t p = 0; p < pulses; ++p) {
                    // 对齐 Channel-major 输出排布校验偏移
                    std::size_t idx = ch * (pulses * samples_per_pulse) + p * samples_per_pulse + s;
                    double actual_val = span[idx];

                    if (p == static_cast<std::size_t>(doppler_bin)) {
                        assert(std::abs(actual_val - expected_peak) < epsilon);
                        std::cout << "[Assert Pass] Channel " << ch << ", Sample " << s 
                                  << ", Doppler " << p << " Peak Val: " << actual_val 
                                  << " (Expected: " << expected_peak << ")" << std::endl;
                    } else {
                        assert(actual_val < (expected_peak / 2.0));
                    }
                }
            }
        }

        span.consume(count);
    }
};

extern template class cycore::sdk::AlgorithmBlockAdapter<RangeDopplerAlgorithm, InputSample, OutputSample>;

int main() {
    fg::Graph graph;
    
    fg::ValueMap params;
    params["num_channels"] = static_cast<std::int64_t>(2);
    params["num_pulses"] = static_cast<std::int64_t>(8);
    params["samples_per_pulse"] = static_cast<std::int64_t>(4);

    auto& source = graph.emplace<SimSource>("source");
    auto& rd_block = graph.emplace<cycore::sdk::AlgorithmBlockAdapter<RangeDopplerAlgorithm, InputSample, OutputSample>>("range_doppler", params);
    auto& sink = graph.emplace<SimSink>("sink");

    graph.connect(source, "out", rd_block, "in", fg::EdgeOptions{128});
    graph.connect(rd_block, "out", sink, "in", fg::EdgeOptions{128});

    graph.init();
    graph.start();
    graph.work_once();
    graph.stop();

    std::cout << "Range-Doppler Algorithm Static Sandbox Test Passed Successfully!" << std::endl;
    return 0;
}
