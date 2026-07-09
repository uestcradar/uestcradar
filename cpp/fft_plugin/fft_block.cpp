#include <flowgraph/blocks/common/fft_block.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>

namespace cy::flowgraph::blocks::common {

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kMagnitudeFloor = 1.0e-12;
constexpr double kInt16Scale = 32767.0;

} // namespace

FFTBlock::FFTBlock(std::size_t fft_size)
    : fft_size_(fft_size) {
    if (fft_size_ == 0) {
        throw std::invalid_argument("FFTBlock fft_size must be greater than zero");
    }
    window_.reserve(fft_size_);
    pending_spectrum_.reserve(fft_size_);
}

void FFTBlock::process_work() {
    if (!pending_spectrum_.empty()) {
        if (!try_publish_pending()) {
            return;
        }
    }

    const std::size_t needed = fft_size_ - window_.size();
    const std::size_t readable = std::min(in.available(), needed);
    if (readable == 0) {
        return;
    }

    auto input = in.get(readable);
    if (input.empty()) {
        return;
    }

    window_.reserve(fft_size_);
    for (std::size_t i = 0; i < input.size(); ++i) {
        window_.push_back(input[i]);
    }
    input.consume(input.size());

    if (window_.size() == fft_size_) {
        compute_pending_spectrum();
        window_.clear();
        try_publish_pending();
    }
}

std::size_t FFTBlock::fft_size() const noexcept {
    return fft_size_;
}

std::size_t FFTBlock::get_frames_computed() const {
    return frames_computed_.load(std::memory_order_acquire);
}

bool FFTBlock::try_publish_pending() {
    while (pending_offset_ < pending_spectrum_.size()) {
        const std::size_t writable = out.available();
        if (writable == 0) {
            return false;
        }

        const std::size_t remaining = pending_spectrum_.size() - pending_offset_;
        auto output = out.reserve(std::min(writable, remaining));
        if (output.empty()) {
            return false;
        }

        const std::size_t count = std::min(output.size(), remaining);
        for (std::size_t i = 0; i < count; ++i) {
            output[i] = pending_spectrum_[pending_offset_ + i];
        }
        output.commit(count);
        pending_offset_ += count;
    }

    pending_spectrum_.clear();
    pending_offset_ = 0;
    frames_computed_.fetch_add(1, std::memory_order_release);
    return true;
}

void FFTBlock::compute_pending_spectrum() {
    pending_spectrum_.assign(fft_size_, 0.0f);
    const double n_inv = 1.0 / static_cast<double>(fft_size_);

    for (std::size_t k = 0; k < fft_size_; ++k) {
        std::complex<double> sum{0.0, 0.0};
        for (std::size_t n = 0; n < fft_size_; ++n) {
            const double angle = -2.0 * kPi * static_cast<double>(k * n) * n_inv;
            sum += static_cast<double>(window_[n]) *
                   std::complex<double>(std::cos(angle), std::sin(angle));
        }

        const double magnitude = std::max(std::abs(sum) * n_inv, kMagnitudeFloor);
        std::size_t shifted_k = (k < fft_size_ / 2) ? (k + fft_size_ / 2) : (k - fft_size_ / 2);
        pending_spectrum_[shifted_k] = static_cast<float>(magnitude);
    }
}

CS16FFTBlock::CS16FFTBlock(std::size_t fft_size)
    : fft_size_(fft_size) {
    if (fft_size_ == 0) {
        throw std::invalid_argument("CS16FFTBlock fft_size must be greater than zero");
    }
    window_.reserve(fft_size_);
    pending_spectrum_.reserve(fft_size_);
}

void CS16FFTBlock::process_work() {
    if (!pending_spectrum_.empty()) {
        if (!try_publish_pending()) {
            return;
        }
    }

    const std::size_t needed = fft_size_ - window_.size();
    const std::size_t readable = std::min(in.available(), needed);
    if (readable == 0) {
        return;
    }

    auto input = in.get(readable);
    if (input.empty()) {
        return;
    }

    window_.reserve(fft_size_);
    for (std::size_t i = 0; i < input.size(); ++i) {
        window_.push_back(input[i]);
    }
    input.consume(input.size());

    if (window_.size() == fft_size_) {
        compute_pending_spectrum();
        window_.clear();
        try_publish_pending();
    }
}

std::size_t CS16FFTBlock::fft_size() const noexcept {
    return fft_size_;
}

std::size_t CS16FFTBlock::get_frames_computed() const {
    return frames_computed_.load(std::memory_order_acquire);
}

bool CS16FFTBlock::try_publish_pending() {
    while (pending_offset_ < pending_spectrum_.size()) {
        const std::size_t writable = out.available();
        if (writable == 0) {
            return false;
        }

        const std::size_t remaining = pending_spectrum_.size() - pending_offset_;
        auto output = out.reserve(std::min(writable, remaining));
        if (output.empty()) {
            return false;
        }

        const std::size_t count = std::min(output.size(), remaining);
        for (std::size_t i = 0; i < count; ++i) {
            output[i] = pending_spectrum_[pending_offset_ + i];
        }
        output.commit(count);
        pending_offset_ += count;
    }

    pending_spectrum_.clear();
    pending_offset_ = 0;
    frames_computed_.fetch_add(1, std::memory_order_release);
    return true;
}

void CS16FFTBlock::compute_pending_spectrum() {
    pending_spectrum_.assign(fft_size_, cy::common::CS16{0, 0});
    const double n_inv = 1.0 / static_cast<double>(fft_size_);

    for (std::size_t k = 0; k < fft_size_; ++k) {
        std::complex<double> sum{0.0, 0.0};
        for (std::size_t n = 0; n < fft_size_; ++n) {
            const double angle = -2.0 * kPi * static_cast<double>(k * n) * n_inv;
            const std::complex<double> sample{
                static_cast<double>(window_[n].i) / kInt16Scale,
                static_cast<double>(window_[n].q) / kInt16Scale};
            sum += sample * std::complex<double>(std::cos(angle), std::sin(angle));
        }

        // 重新缩放回 Int16 范围并进行溢出保护裁剪
        std::complex<double> complex_val = sum * n_inv * kInt16Scale;
        double real_val = std::clamp(complex_val.real(), -32768.0, 32767.0);
        double imag_val = std::clamp(complex_val.imag(), -32768.0, 32767.0);

        std::size_t shifted_k = (k < fft_size_ / 2) ? (k + fft_size_ / 2) : (k - fft_size_ / 2);
        pending_spectrum_[shifted_k] = cy::common::CS16{
            static_cast<std::int16_t>(std::round(real_val)),
            static_cast<std::int16_t>(std::round(imag_val))
        };
    }
}

} // namespace cy::flowgraph::blocks::common
