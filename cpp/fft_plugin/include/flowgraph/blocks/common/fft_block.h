#ifndef CYCORE_FLOWGRAPH_FFT_BLOCK_H
#define CYCORE_FLOWGRAPH_FFT_BLOCK_H

#include <flowgraph/block.h>
#include <flowgraph/port.h>

#include <common/data_types.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cy::flowgraph::blocks::common {

class FFTBlock : public cy::flowgraph::Block<FFTBlock> {
public:
    cy::flowgraph::PortIn<float> in;
    cy::flowgraph::PortOut<float> out;
    CY_MAKE_REFLECTABLE(FFTBlock, in, out);

    explicit FFTBlock(std::size_t fft_size = 1024);

    void process_work();

    std::size_t fft_size() const noexcept;
    std::size_t get_frames_computed() const;

private:
    bool try_publish_pending();
    void compute_pending_spectrum();

    std::size_t fft_size_ = 1024;
    std::vector<float> window_;
    std::vector<float> pending_spectrum_;
    std::size_t pending_offset_ = 0;
    std::atomic<std::size_t> frames_computed_{0};
};

class CS16FFTBlock : public cy::flowgraph::Block<CS16FFTBlock> {
public:
    cy::flowgraph::PortIn<cy::common::CS16> in;
    cy::flowgraph::PortOut<cy::common::CS16> out;
    CY_MAKE_REFLECTABLE(CS16FFTBlock, in, out);

    explicit CS16FFTBlock(std::size_t fft_size = 1024);

    void process_work();

    std::size_t fft_size() const noexcept;
    std::size_t get_frames_computed() const;

private:
    bool try_publish_pending();
    void compute_pending_spectrum();

    std::size_t fft_size_ = 1024;
    std::vector<cy::common::CS16> window_;
    std::vector<cy::common::CS16> pending_spectrum_;
    std::size_t pending_offset_ = 0;
    std::atomic<std::size_t> frames_computed_{0};
};

class DerivativeBlock : public cy::flowgraph::Block<DerivativeBlock> {
public:
    cy::flowgraph::PortIn<cy::common::CS16> in;
    cy::flowgraph::PortOut<cy::common::CS16> out;
    CY_MAKE_REFLECTABLE(DerivativeBlock, in, out);

    DerivativeBlock() = default;

    void process_work() {
        const std::size_t readable = in.available();
        const std::size_t writable = out.available();
        const std::size_t count = std::min(readable, writable);
        if (count == 0) return;

        auto input = in.get(count);
        auto output = out.reserve(count);
        if (input.empty() || output.empty()) return;

        for (std::size_t i = 0; i < count; ++i) {
            output[i].i = input[i].i - last_val_.i;
            output[i].q = input[i].q - last_val_.q;
            last_val_ = input[i];
        }

        input.consume(count);
        output.publish(count);
    }

private:
    cy::common::CS16 last_val_{0, 0};
};

} // namespace cy::flowgraph::blocks::common

#endif // CYCORE_FLOWGRAPH_FFT_BLOCK_H
