# pcie_source

PCIe 实时数据源 Worker：接收 **8 路 IQ**，选择一路，使用现有 `IQFrame` 契约发送给下游 Worker。

> [!NOTE]
> **正式 SDK 输出尚未实现**，需使用 `--capture-only`。按用户最新要求检查相邻 RX 时间戳差值 **98304**，异常完整记录且不清空 CPI。优化后 channel 0 的 30 秒实测达到 **30.693 MS/s**，112400 次时间戳比较无异常；仍不等同于 DMA 完整性证明，见 [最新实测](tests/throughput-results.md)。Harbor 正式发布仍未完成。

## 已确定的方案

- 底层迁移来源：`/home/zikun/code/common/cycore/lib/du/resource/device/pcie/`；IQ 重组参考相邻 `pcie_device_handle.cpp`。
- 沿用“4 根光纤 → 8 路 CS16”的现有映射，不实现 16 路扩展。
- Source 没有上游 SDK 输入；输出目的地是下游 Worker，不是硬件光纤或 RF TX。
- 拟通过 `--channel 0..7` 选择一路，默认 `0`。
- 硬件无法提供完整 CPI 边界及 SDK 所需参数，因此每 **751206** 个选中通道复数采样点组成一个**软件 CPI**，`pulse_count=64`，与当前 `signalsource` 的单帧形状一致。
- 循环使用同版 `signalsource` 的 `CPI0..CPI9` 参数模板；只把 `input.bin` 替换为实时 PCIe 样本，`cpi_index` 持续递增。废止此前独立的分块和假参数默认方案。
- 保持 `IQFrame` 的类型、版本及字段布局不变，不新增契约。
- 下游缓冲满时允许丢弃完整软件 CPI，但每个交付帧必须含连续、有效的 `751206` 点。切分不受下游背压影响，不发半帧、不补零、不跨采集缺口拼接，详见 [整帧丢弃策略](src/README.md#整帧丢弃策略)。
- 首版不包含多路广播、运行时切换、IQ 样本落盘、脉压或 RD 算法；仅保存时间戳异常诊断。

> [!IMPORTANT]
> 样本来自真实采集，但 CPI 边界及雷达参数是软件构造的。该模式用于链路联调，不代表真实相干积累周期，不能据此宣称距离、速度或脉压结果具有物理意义。启动日志必须标明 `synthetic_metadata=true`；该标记不新增到帧字段中。

## 整体数据流

```text
FPGA / PCIe：4 根光纤
    → 接收与校验
    → 8 路 IQ 布局解析、选择一路
    → 每 751206 点组成单路软件 CPI + signalsource 参数模板
    → Output<IQFrame>
    → Output SHM → Sidecar → 下游 Sidecar → 下游 Worker
```

底层负责硬件访问，Worker 负责选路、分块及 SDK 输出，Sidecar 负责传输。未选中的数据正常消费并丢弃，只建立一个输出端口。

## 按需阅读

实施进度：[实施计划](tasks/plan.md) · [TODO 清单](tasks/todo.md)。离线检查和 Docker 构建已通过，停在独立硬件采集关卡，不越过失败项进入下游验收。

| 目录 | 内容 |
| --- | --- |
| [src/](src/README.md) | 通道选择、软件 CPI、SDK 输出、背压及退出 |
| [pcie/](pcie/README.md) | 8 路映射、[光口控制表协议](pcie/control-protocol.md)、底层迁移及指针寿命 |
| [pcie_config/](pcie_config/README.md) | signalsource 参数模板、硬件配置及 Docker 部署约定 |
| [tests/](tests/README.md) | 无硬件检查、硬件联调、链路及故障验收 |

```text
pcie_source/
├── README.md
├── CMakeLists.txt        # C/C++20 构建与 CTest
├── Dockerfile            # ARM64 镜像；构建时运行无硬件检查
├── src/
│   └── README.md
├── pcie/
│   └── README.md
├── pcie_config/
│   └── README.md
└── tests/
    └── README.md
```

目录中现已包含源码、配置和测试，上图仅展示文档入口。访问设备前核对目标板卡、固件、物理地址及权限，见 [部署清单](pcie_config/README.md#实施前需确认)。

## 构建与独立采集

在本目录、已安装 SDK 的环境构建：

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

镜像必须经 **ARM64 构建验证 → push Harbor → `.64` 按 digest pull → 运行**。构建机负责源码和编译，目标机只拉取运行；不再拷贝源码到目标机或使用 save/load 代替发布。完整命令见 [Harbor 发布与部署](pcie_config/README.md#harbor-发布与目标机部署)。

在 `.64` 将实际发布的完整镜像 digest 引用设为 `IMAGE_REF` 后拉取。硬件初始化会写寄存器，确认部署条件再运行：

```bash
: "${IMAGE_REF:?请设置 Harbor 发布记录中的完整 digest 引用}"
docker pull "$IMAGE_REF"
mkdir -p "$PWD/pcie-logs"
docker run --rm --name pcie-source-capture \
  --device /dev/mem:/dev/mem --cap-add SYS_RAWIO \
  --mount "type=bind,src=$PWD/pcie-logs,dst=/logs" \
  "$IMAGE_REF" --capture-only --channel 0 --duration-seconds 30 \
  --timestamp-errors /logs/rx_timestamp_errors.jsonl
```

不需要 Sidecar 或 SHM。退出码 `0` 只表示观察到 IQ、完成软件分块且无原有接收错误，**不表示时间戳连续**；RX 校验结果看 `[timestamp-check] strict_continuous`，异常在宿主机 `pcie-logs/rx_timestamp_errors.jsonl`。`2` 表示无有效 IQ/未成帧或接收错误，`1` 表示参数、初始化或日志 I/O 错误。仍标记 `integrity_verified=false`；完整 CLI 见 `--help`。

此前传源码、导入依赖镜像并在目标机构建属于临时开发验证，不是正式部署；历史记录保留在 [hardware-results.md](tests/hardware-results.md)。当前尚无本项目镜像的 Harbor push/pull 验收结果；遇到拉取失败需报告阻塞，不再自动绕过。

## 与 signalsource 的替换关系

本目录根部 `Dockerfile` 已创建，沿用 `signalsource` 的基础镜像、参数数据来源、SDK 输出 SHM 和 Sidecar 接入方式。正式输出完成后，目标是换数据源镜像及必要硬件配置，下游保持同一 `IQFrame` 输入接口；当前镜像还不能作为 signalsource 的正式替代。具体约定见 [Docker 部署](pcie_config/README.md#docker-部署约定)。

输出契约、形状和参数模板对齐，但实时样本不等于离线样本。原有针对离线数据的哈希、数值及算法结果校验不适用于实时采集；模板也不代表真实硬件时序。
