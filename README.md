# UESTC 雷达信号处理平台 (uestcradar)

本项目是一个结合了 **MATLAB 原型算法仿真** 与 **C++ 高性能流图算法** 的混合雷达信号处理开发平台，用于实现实时的雷达数字信号处理流水线。

---

## 目录架构

```text
uestcradar/
├── matlab/                       # MATLAB 仿真与算法验证（核心入口）
│   ├── README.md                 # MATLAB 专属使用指南
│   ├── algorithm/                # 基础算法 M 函数 (PC, CFAR 等)
│   ├── radar_gui.m               # 交互式雷达目标回波 GUI 仿真器
│   ├── lfm_tx.m                  # LFM 发射信号生成仿真
│   └── parse_bin.m               # 二进制雷达抓取 Cube 数据解析与校准
├── workspace/                    # Worker 示例与基础设施
│   ├── examples/
│   │   ├── KT1/                  # 数据源、采集、级联 Worker 与录制设计
│   │   │   ├── cascade_worker/
│   │   │   ├── pcie_source/
│   │   │   ├── signalsink/
│   │   │   └── signalsource/
│   │   ├── KT2/                  # C++ 脉冲压缩算法开发基座
│   │   └── KT3/                  # Qt5 距离-多普勒算法开发基座
│   ├── .diagrams/                # 架构与数据路径图（隐藏目录）
│   ├── infra/                    # common、proto、sdk、sidecar、web
│   └── TARGET_ARCHITECTURE.md     # 总体架构文档
└── LICENSE                       # 项目授权协议
```

---

## MATLAB 仿真工具箱 (MATLAB 部分)

`matlab` 目录提供了完整的 Range-Doppler（距离-多普勒）二维处理流程和雷达回波仿真分析工具。

### 1. 核心功能特性

* **交互式 GUI 动态分析仪 (`radar_gui.m`)**：
  提供一站式图形用户面板。支持加载 TX/RX 二进制回波数据，动态调整 DSP 参数（如 FFT 变换点数、MTI 强度、CFAR 判决门限），实时渲染播放四宫格 Range-Doppler 谱图及切片对比线，并支持将回放过程一键导出为 GIF 动图。
* **自适应距离零点标定算法 (`calibrate_range_zero.m`)**：
  利用发射与接收通道间的互相关峰值捕获直达波（Leakage）信号，自动标定绝对距离的零点，消除物理线缆和硬件传输链路带来的时延。
* **静止杂波抑制（MTI 算子库）**：
  提供了复均值相减（`mti_avg.m`）和双脉冲对消（`mti_two_pulse.m`）两种经典的动目标指示算法，有效抑制地杂波并突出运动目标。
* **元数据驱动机制**：
  支持直接读取并解析多通道 `CS16` 格式交错二进制数据。雷达射频参数、PRI、频段及通道数等均由所在目录的 `metadata.json` 自动加载解析，无需硬编码。

> [!TIP]
> 📖 关于 MATLAB 工具箱的依赖安装、详细数据包下载准备以及 GUI 运行步骤指南，请参阅：
> **[MATLAB 子目录专属说明文档 (matlab/README.md)](matlab/README.md)**

---

## C++ Worker 与基础设施

当前 C++ 开发入口位于 `workspace/`。Worker 使用 SDK 的 `Input<T>`、`Output<T>`
帧接口；共享内存、Sidecar、网络传输和 Web 控制面位于 `workspace/infra/`。
总体设计见 [目标架构](workspace/TARGET_ARCHITECTURE.md)。

`KT1` 包含以下模块：

| 模块 | 用途与当前状态 |
| --- | --- |
| [cascade_worker](workspace/examples/KT1/cascade_worker/README.md) | 独立级联 Worker 示例，支持 source、operator、sink 角色 |
| [signalsource](workspace/examples/KT1/signalsource/README.md) | 加载 CPI0–CPI9 离线数据并输出 IQFrame |
| [pcie_source](workspace/examples/KT1/pcie_source/README.md) | PCIe IQ 采集与时间戳诊断；目前仅支持 capture-only，尚未实现 SDK 下游输出 |
| [signalsink](workspace/examples/KT1/signalsink/SPEC.md) | 数据录制规格与交互原型，尚非可运行 Worker |

Dockerfile 和 Compose 配置随各示例或基础设施模块存放，构建与运行请使用对应目录的说明。
正式镜像发布流程见 [Docker Release](.agents/skills/docker-release/SKILL.md)。

## 算法开发基座 (单机测试环境)

在 `workspace/examples/` 目录下提供了单机测试黑盒环境（内置测试数据源与结果自动校验）。算法开发者在本地一键启动测试环境后，即可专心编写 C++ / Qt 算法代码，无需关心底层传输细节。

### 算法开发指南

| 算法开发基座 | 适用场景 | 说明文档 (相对路径) |
| :--- | :--- | :--- |
| **`KT2`** | C++ 一维匹配滤波 / 脉冲压缩算法开发 | **[脉冲压缩算法开发指南](workspace/examples/KT2/README.md)** |
| **`KT3`** | Qt5 框架下二维距离-多普勒 (RDMap) 算法开发 | **[Qt5 RD 图算法开发指南](workspace/examples/KT3/README.md)** |

> [!TIP]
> 📖 关于各种雷达数据帧（IQ 数据、脉冲压缩数据、RD 图）的具体字段含义和读取方法，请参阅：
> **[SDK 数据读写指南](workspace/infra/sdk/README.md)**
