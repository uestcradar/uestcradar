#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace fft_data = cycore::algorithm::fft_block;

class FFTAlgorithm {
public:
    explicit FFTAlgorithm(const cycore::sdk::Params& params) {
        // 🟢 提取维度参数，统一使用 int64_t
        fft_size_ = params.get<std::int64_t>("fft_size", static_cast<std::int64_t>(fft_data::kDefaultSamplesPerPulse));
        num_channels_ = params.get<std::int64_t>("num_channels", static_cast<std::int64_t>(fft_data::kDefaultNumChannels));

        if (fft_size_ <= 0 || num_channels_ <= 0) {
            throw std::invalid_argument("Dimensions must be positive");
        }

        // 🟢 预计算 DFT 旋转因子以提升实时乘加效率
        twiddle_factors_.resize(fft_size_);
        const double pi = 3.14159265358979323846;
        for (std::int64_t m = 0; m < fft_size_; ++m) {
            double angle = 2.0 * pi * m / fft_size_;
            twiddle_factors_[m].cos_val = static_cast<float>(std::cos(angle));
            twiddle_factors_[m].sin_val = static_cast<float>(std::sin(angle));
        }
    }

    bool work(cycore::sdk::Reader<fft_data::InputSample>& in,
              cycore::sdk::Writer<fft_data::OutputSample>& out) {
              
        // 🟢 读写锁定 16 通道、1 脉冲、1024 采样点 (64KB 数据)
        auto input = in.read_cube(num_channels_, 1, fft_size_);
        if (!input) {
            return false;
        }

        auto output = out.reserve_cube(num_channels_, 1, fft_size_);
        if (!output) {
            return false;
        }

        const fft_data::InputSample* in_ptr = input->data();
        fft_data::OutputSample* out_ptr = output->data();

        // 🟢 雷达多通道交织 DFT 相干积累计算
        // 缩放因子设为 1/sqrt(N) 即 1/32.0f，保持积累前后信号能量功率恒定
        const float scale = 1.0f / 32.0f;

        auto clamp = [](float val) -> std::int16_t {
            if (val >= 32767.0f) return 32767;
            if (val <= -32768.0f) return -32768;
            return static_cast<std::int16_t>(std::round(val));
        };

        for (std::int64_t ch = 0; ch < num_channels_; ++ch) {
            for (std::int64_t k = 0; k < fft_size_; ++k) {
                float sum_i = 0.0f;
                float sum_q = 0.0f;

                for (std::int64_t n = 0; n < fft_size_; ++n) {
                    // 跨步寻址：Index(n, ch) = n * num_channels + ch
                    const auto& sample = in_ptr[n * num_channels_ + ch];

                    // 检索对应的旋转因子索引：(k * n) % N
                    std::int64_t idx = (k * n) % fft_size_;
                    const auto& twiddle = twiddle_factors_[idx];

                    // 复数乘法累加：(sample.i + j*sample.q) * (cos - j*sin)
                    sum_i += static_cast<float>(sample.i) * twiddle.cos_val + static_cast<float>(sample.q) * twiddle.sin_val;
                    sum_q += static_cast<float>(sample.q) * twiddle.cos_val - static_cast<float>(sample.i) * twiddle.sin_val;
                }

                // 写入输出交织槽中并进行饱和截断
                out_ptr[k * num_channels_ + ch].i = clamp(sum_i * scale);
                out_ptr[k * num_channels_ + ch].q = clamp(sum_q * scale);
            }
        }
        
        return true;
    }

private:
    struct TwiddleFactor {
        float cos_val;
        float sin_val;
    };

    std::int64_t fft_size_ = 1024;
    std::int64_t num_channels_ = 16;
    std::vector<TwiddleFactor> twiddle_factors_;
};

CYCORE_EXPORT_ALGORITHM(
    "fft_plugin",
    "algorithm.fft_cs16",
    FFTAlgorithm,
    cycore::algorithm::fft_block::InputSample,
    cycore::algorithm::fft_block::OutputSample
)
