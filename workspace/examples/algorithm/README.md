# 完整 CPI IQ v3 脉压算子基座

本目录提供可交给脉压算法开发者继续开发的 Worker 基座：

```text
完整连续 CPI IQFrame 1:3
  -> IQ v3 输入适配层
  -> 原 echo_process 数学核心
  -> 每个脉冲一个 PulseCompressionFrame 2:2
```

输入适配层位于 `src/iq_adapter.hpp`。它使用 `dequantization_scale` 将 CS16 还原为
`std::complex<double>`，并从 Metadata 恢复 pulse time、phase、frequency 和 coherent
weight。完整连续回波只进入原算法一次，取窗位置仍由原算法按照
`round(pulse_time[i] * sample_rate)` 决定，绝不将输入平均切成 64 段。

原算法数学文件 `echo_process.cpp/.hpp` 和 `radar_params.hpp` 从权威工程原样引入。当前
输出适配将原算法产生的每一行脉压结果写成一个 `PulseCompressionFrame`，通过
`pulse_index/pulses_per_cpi` 保留 CPI 内顺序。这样单个下游帧保持在 4 MiB 以内。

## 构建

```bash
docker build --pull -t my-radar-algorithm:dev .
```

## 启动完整开发链

```bash
export CPI0_DATA_DIR=/path/to/CPI0
export SIGNALSOURCE_IMAGE=registry.chengyistudio.com/cxx/signalsource:latest
export ALGORITHM_IMAGE=my-radar-algorithm:dev
docker-compose -f docker-compose.infra.yaml up -d --no-build
docker-compose -f docker-compose.infra.yaml logs -f algorithm pulse-sink
```

Compose 默认使用 `SLOT_COUNT=8`、`MAX_PAYLOAD_BYTES=4194304`；IQ Leg 是 `1:3`，
脉压 Leg 是 `2:2`。Source 的一个输入 CPI 会产生 64 个脉压帧，因此有限帧测试时不要
把 Sink 的帧数误设为 CPI 数；默认 Sink 持续消费。

## 真实数据适配测试

```bash
cmake -S src -B build \
  -DCPI0_TEST_DATA_DIR=/path/to/CPI0
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`cpi-adapter-real-data` 会分别通过适配层和直接参考路径运行同一份量化输入，并逐点比较
脉压矩阵、RD 矩阵及坐标轴。
