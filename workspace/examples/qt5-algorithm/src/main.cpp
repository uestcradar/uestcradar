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
        // 1. 读取真实脉压数据并缓存，连续收齐 64 个脉冲才处理。
        auto pulse = input.read();
        const auto pulse_metadata = pulse.metadata();
        cpi.push(pulse_metadata, pulse.data()[0]);
        if (!cpi.ready()) continue;

        // 2. 创建一个与真实距离门数量匹配的 RDFrame。
        const RDMetadata metadata{
            .channel_index = 0,
            .range_bin_count = cpi.range_bin_count(),
            .doppler_bin_count = kDopplerBinCount,
            .range_resolution_m = cpi.range_resolution_m(),
            .velocity_resolution_mps = kVelocityResolutionMps,
        };
        auto rd = output.create(metadata, pulse);

        // 3. 用自己的 RD 算法替换这里的占位处理。
        auto rd_data = rd.data();
        const auto result = compute_rd(cpi, rd_data);

        // 4. 保存最新 RDMap 图像，方便本地查看结果。
        save_rd_map_pgm(rd_data, "output/rdmap_result.pgm");
        qInfo() << "RD peak=" << result.peak_range_bin << ','
                << result.peak_doppler_bin
                << "magnitude=" << result.peak_magnitude;

        // 5. 提交 RDMap 给下游，并开始积累下一个 CPI。
        output.write(std::move(rd));
        cpi.clear();
    }
}
