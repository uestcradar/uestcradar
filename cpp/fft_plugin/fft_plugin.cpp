#include <flowgraph/plugin.h>
#include <flowgraph/blocks/common/fft_block.h>

CY_PLUGIN("fft_plugin", "1.0.0", "FFT algorithm plugin", "cycore",
          CY_REGISTER_BLOCK("algorithm.fft_double", ::cy::flowgraph::blocks::common::FFTBlock)
          CY_REGISTER_BLOCK("algorithm.fft_cs16", ::cy::flowgraph::blocks::common::CS16FFTBlock)
          CY_REGISTER_BLOCK("algorithm.derivative", ::cy::flowgraph::blocks::common::DerivativeBlock))
