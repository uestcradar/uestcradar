# SignalSource RawIQFrame 变体

单通道确定性仿真 Source，输出 `RawIQFrame 4:1`，用于 [PCIe→SignalSink 计划](../pcie_source/SPEC.md) 的先行传输/保存验证。独立目录和镜像契约避免运行模式与静态镜像标签冲突；旧 [signalsource](../signalsource/README.md) 及算法链保持不变。

## 数据与运行

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/new-sdk
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/signalsource-raw-iq --frames 64 --samples 8192 --sample-rate 819200
```

运行前，Sidecar 必须创建 4:1 输出 Ring，并通过 `UESTCRADAR_DOWNSTREAM_SHM_NAME` 绑定。打开端口最多等 3 秒；Ring 满时可响应 SIGINT/SIGTERM，连续阻塞 5 秒报错。

- `--frames`：帧数，默认 0 表示持续发送。
- `--samples`：每帧单通道样本数，默认 8192，范围 1～1048576；不得超出部署 Ring 容量。
- `--sample-rate`：仿真发送节奏，默认 30720000 点/秒，0 表示不节流。不是硬件实测速率，也不是新增帧字段。
- IQ 使用固定 xorshift32 图样，每帧首个复数样本带帧索引标记。
- TX/RX 是明确的测试整数，包含全 F 与超过 JavaScript 精确整数范围的数值，不声称代表硬件时钟。
- 每帧 `channel_count=1`，不使用 CPI、脉冲或雷达参数。

## 保存与校验

先开启 SignalSink 录制，再启动有限帧 Source。完整 [Sidecar→SignalSink 测试](../pcie_source/tests/README.md)验证每个时间戳和全部 IQ 字节，拒绝损坏及不完整文件。SignalSink 仍保存完整裸帧，不需要认识本帧的 Metadata。

## 镜像

根目录 Dockerfile 可通过现有 Worker 发布流程构建，输出标签固定 `4:1`。必须先发布包含 RawIQFrame 及非阻塞输出的新 SDK，并以固定 `ALGO_BASE` 引用构建；旧 SDK 会编译失败。

原生仿真文件验证不等于 PCIe 硬件、Harbor 发布或持续磁盘吞吐验收，见 [分级证据](../pcie_source/tests/raw-iq-results.md)。
