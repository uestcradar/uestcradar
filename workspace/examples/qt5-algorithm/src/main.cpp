#include <data.h>

#include "my_rd_algorithm.hpp"

#include <QCoreApplication>
#include <QDebug>

#include <utility>

using namespace uestcradar;
using namespace radar_qt_example;

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    Input<PulseCompressionFrame> input;
    Output<RDFrame> output;
    CpiBuffer cpi;

    while (true) {
        // 1. 读取脉压数据并缓存，连续收齐 64 个脉冲才处理。
        auto pulse = input.read();
        const auto pulse_metadata = pulse.metadata();
        cpi.push(pulse_metadata, pulse.data()[0]);
        if (!cpi.ready()) continue;

        // 2. 填写输出参数，创建一个不超过 32 MiB 的 RDFrame。
        const RDMetadata metadata{
            .channel_index = 0,
            .range_bin_count = cpi.range_bin_count(),
            .doppler_bin_count = kDopplerBinCount,
            .range_resolution_m = pulse_metadata.range_resolution_m,
            .velocity_resolution_mps = kVelocityResolutionMps,
        };
        auto rd = output.create(metadata, pulse);

        // 3. 计算 RDMap，打印峰值并提交给下游。
        const auto result = compute_rd(cpi, rd.data());
        qInfo() << "RD peak=" << result.peak_range_bin << ','
                << result.peak_doppler_bin
                << "magnitude=" << result.peak_magnitude;
        output.write(std::move(rd));
        cpi.clear();
    }
}
