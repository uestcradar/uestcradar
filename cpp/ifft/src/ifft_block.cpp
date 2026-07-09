#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace fft_data = cycore::algorithm::fft_block;

class IFFTAlgorithm {
public:
    explicit IFFTAlgorithm(const cycore::sdk::Params& params) {
        // 🟢 提取维度参数，统一使用 int64_t
        fft_size_ = params.get<std::int64_t>("fft_size", static_cast<std::int64_t>(fft_data::kDefaultSamplesPerPulse));
        num_channels_ = params.get<std::int64_t>("num_channels", static_cast<std::int64_t>(fft_data::kDefaultNumChannels));

        if (fft_size_ <= 0 || num_channels_ <= 0) {
            throw std::invalid_argument("Dimensions must be positive");
        }

        // 🟢 预计算 IDFT 正旋转因子 (旋转因子的角度符号为正)
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
              
        // 🟢 读写锁定多通道数据，单次读取一个 Cube 大小
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

        // 🟢 雷达多通道交织 IDFT (IFFT) 频域到时域转换计算
        // 同样在逆变换做 1/sqrt(N) 缩放 (1/32.0f)，使得正反变换连通后总缩放为 1/N
        const float scale = 1.0f / 32.0f;

        auto clamp = [](float val) -> std::int16_t {
            if (val >= 32767.0f) return 32767;
            if (val <= -32768.0f) return -32768;
            return static_cast<std::int16_t>(std::round(val));
        };

        for (std::int64_t ch = 0; ch < num_channels_; ++ch) {
            for (std::int64_t n = 0; n < fft_size_; ++n) {
                float sum_i = 0.0f;
                float sum_q = 0.0f;

                for (std::int64_t k = 0; k < fft_size_; ++k) {
                    // 跨步寻址：Index(k, ch) = k * num_channels + ch
                    const auto& sample = in_ptr[k * num_channels_ + ch];

                    // IDFT 因子索引：(k * n) % N
                    std::int64_t idx = (k * n) % fft_size_;
                    const auto& twiddle = twiddle_factors_[idx];

                    // 复数乘法累加 (IDFT 定义使用正指数项，所以是相加)
                    // (I + jQ) * (cos + jsin) = (I*cos - Q*sin) + j(Q*cos + I*sin)
                    sum_i += static_cast<float>(sample.i) * twiddle.cos_val - static_cast<float>(sample.q) * twiddle.sin_val;
                    sum_q += static_cast<float>(sample.q) * twiddle.cos_val + static_cast<float>(sample.i) * twiddle.sin_val;
                }

                // 写入输出时域交织槽中并进行饱和截断
                out_ptr[n * num_channels_ + ch].i = clamp(sum_i * scale);
                out_ptr[n * num_channels_ + ch].q = clamp(sum_q * scale);
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
    "ifft_plugin",
    "algorithm.ifft",
    IFFTAlgorithm,
    cycore::algorithm::fft_block::InputSample,
    cycore::algorithm::fft_block::OutputSample
)
