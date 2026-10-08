# pcie_source

PCIe 接收 Worker：从既有八路映射中选择**一路**，输出 `RawIQFrame 4:1`，由 SignalSink 原样保存裸帧。不新增硬件发射功能，不加载软件 CPI 或雷达模板。

> [!NOTE]
> 单通道组帧、SDK 输出和有界背压已实现；原生 ARM 离线测试、服务器上的仿真 Source→Sidecar→SignalSink 文件验证已通过。**新 PCIe 路径尚未完成 Harbor 发布和真实硬件 10/60 秒录制验收**，不能将仿真或旧版本采集结果当成当前硬件通过记录。见 [当前证据](tests/raw-iq-results.md)。

## 数据流与协议

```text
PCIe 描述字 → 原有选路/双快照检查 → 控制表与单通道硬件帧配对
    → 有界完整帧队列 → Output<RawIQFrame>
    → Source Sidecar → 接收 Sidecar → SignalSink → USINK001 裸帧文件
```

- `--channel 0..7`，默认 0；一次只输出所选一路，`channel_count=1`。
- Metadata 仅 TX/RX 原始 uint64 时间戳、通道数、每通道采样点数；不解释时间单位。
- IQ 沿用小端 CS16（I 后 Q）、通道行/采样点列排列。当前硬件帧每通道 8192 点，不是 64 脉冲软件 CPI。
- 24 字节 Metadata + 32768 字节 IQ = 32792 字节 SDK Payload；完整裸帧 32856 字节。
- 未选中的七路不进入输出流。控制包/BIT 不冒充 IQ；SDK Envelope 时间戳不替代硬件时间戳。
- 旧 `IQFrame 1:3`、旧 [signalsource](../signalsource/README.md) 与算法链不变；本 Worker 不再输出旧协议，也不直接接旧脉压/RD。
- 仿真替身为 [signalsource_raw_iq](../signalsource_raw_iq/README.md)，同样输出 4:1。
- **SignalSink 核心代码和文件格式不变**，只将输入绑定为 `4:1`。

## 构建与使用

先安装包含 RawIQFrame、`try_create()` 和带超时 Output 构造函数的新 SDK：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sdk -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pcie_source --help
```

```text
pcie_source [--capture-only] [--channel 0..7] [--queue-frames 1..32768]
            [--duration-seconds N] [--pcie-config-dir PATH] [--timestamp-errors PATH]
```

默认连续运行，队列 512 帧（含输出线程在途帧，约 16 MiB IQ）。`--capture-only` 使用相同选路/组帧检查，但不打开 SDK 输出端口；不是落盘模式。`--data-root` 已删除，镜像不再携带 CPI 数据。

正式输出要求对应 Sidecar 已创建 `4:1` 输出 SHM，并通过 `UESTCRADAR_DOWNSTREAM_SHM_NAME` 绑定。端口打开最多等待 3 秒；输出使用非阻塞获取。SIGINT/SIGTERM 或运行时长到达后停止接收并排空已接受队列，最多等待 5 秒；失败明确退出，不静默丢帧续跑。

退出码：0 表示完成非空帧、交付/诊断计数一致且可检测接收/时间戳检查通过；2 表示数据不足或检查未通过；1 表示参数、初始化、配对、队列、输出或日志异常。**0 不证明 DMA 所有权、ADC 无漏采或文件已经保存**，还需端到端核对。

## 部署和验收

硬件访问会按现有配置初始化寄存器，先核对设备占用、板卡地址、预留内存及权限。硬件验收使用原生 ARM 验证并发布至 Harbor 的不可变候选镜像；不将临时挂载的开发二进制当成正式部署。

先完成仿真闭环，再做真实单通道 10 秒冒烟和 60 秒完整录制。源头先停并排空在途帧，随后停止录制并等待文件同步。详细步骤见 [测试说明](tests/README.md)及 [部署约定](pcie_config/README.md)。Web 模式保持 strict-RDMA，独立 TCP 诊断不冒充 RDMA 验收。

历史单通道约 30.7 MS/s、CS16 约 123 MB/s；现有盘长写约 101 MB/s。有限时长文件完整保存与长期稳定写入必须分开报告，不降采样、不压缩、不用八通道压力指标替代本次真实需求。

## 进一步阅读

- [规格](SPEC.md) · [计划](tasks/plan.md) · [任务状态](tasks/todo.md)
- [选路、组帧及输出实现](src/README.md)
- [底层接收](pcie/README.md) · [控制表与光口协议](pcie/control-protocol.md)
- [旧版独立采集性能记录](tests/throughput-results.md)：历史证据，不覆盖新路径。
