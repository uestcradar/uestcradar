# UESTC Radar (雷达信号处理工具箱)

这是一个基于 MATLAB 的雷达信号处理与分析工具箱，主要用于解析、处理和动态可视化雷达原始双向信号（TX 发射参考与 RX 接收回波）。

该仓库包含了一套完整的 Range-Doppler（距离-多普勒）处理流程，涵盖了脉冲压缩、动目标指示（MTI）、自适应距离零点标定、恒虚警率检测（CFAR）等核心算法，并提供了一个交互式图形用户界面（GUI）方便用户进行数据回放和动态分析。

## 目录结构

```text
uestcradar/
├── algorithm/                 # 核心数字信号处理（DSP）算法模块
│   ├── calibrate_range_zero.m # 自动寻找直漏峰值，标定距离零点
│   ├── cfar_detector.m        # 恒虚警率（CFAR）检测器
│   ├── doppler_fft.m          # 多普勒 FFT 处理
│   ├── make_hann_window.m     # 汉宁窗生成
│   ├── mti_avg.m              # MTI (复均值相减)
│   ├── mti_two_pulse.m        # MTI (双脉冲对消)
│   ├── process_one_window.m   # 单个滑窗数据的完整 RD 处理流水线
│   ├── pulse_compression.m    # 脉冲压缩（匹配滤波）
│   ├── read_contiguous_pri_block.m # 连续 PRI 数据块读取
│   ├── read_cs16_channel_file.m    # CS16 格式通道数据读取
│   └── read_json_file.m       # JSON 元数据解析
├── lfm_tx.m                   # LFM 发射波形生成或相关脚本
├── parse_bin.m                # 原始双向数据（TX 和 RX）解析与 MAT 格式导出工具
├── parse_capture.m            # 脚本化滑窗 Range-Doppler 动图（GIF）生成与分析
├── radar_gui.m                # 【核心入口】雷达滑窗 Range-Doppler 动态分析仪 GUI
├── test_calibrate_zero.m      # 距离零点自动标定（脉压互相关峰值）测试与调试脚本
└── LICENSE                    # MIT 开源协议许可文件
```

## 功能特性

1. **交互式 GUI 分析仪** (`radar_gui.m`)：提供方便的面板用于加载数据、自动解析元数据、动态调整 DSP 参数（如 FFT 点数、MTI 强度、CFAR 门限等），并实时播放四宫格 Range-Doppler 对比图。
2. **自动化零点标定**：通过捕获直达波/泄漏信号的最强峰值，自适应标定绝对距离的零点，消除硬件线缆和链路带来的延迟。
3. **支持多通道 CS16 格式**：能够直接读取和解交织底层采集硬件生成的 `int16` / `CS16` 交错二进制数据格式。
4. **元数据驱动**：参数无需在代码中硬编码，所有雷达射频和基带参数（采样率、PRI、频段、通道数等）均由所在目录的 `metadata.json` 自动解析加载。

## 依赖要求

- **MATLAB** (推荐 R2021a 或以上版本，主要为了兼容较新的 `uifigure` 及相关 UI 组件)
- 无需额外的特殊工具箱依赖，核心函数均由手写或使用内置基础函数实现。

## 使用说明

### 1. 准备数据

本仓库代码处理的雷达数据可从以下地址获取：
[https://chengyistudio.com/downloads/data/Helium](https://chengyistudio.com/downloads/data/Helium)

程序依赖于底层采集生成的 `.bin` 文件及对应的 `metadata.json` 配置文件。你的数据目录结构应当如下所示：

**发射侧参考信号目录**：
- `tx_waveform.bin` (发射的参考二进制基带数据)
- `metadata.json` (描述发射信号参数，包含 `sample_rate`, `PRI`, `format`, `channels` 等)

**接收侧回波目录**：
- `cpi_001.bin`, `cpi_002.bin` ... (连续存储的接收回波数据)
- `metadata.json` (描述接收数据参数，包含 `format`, `channels` 等)

### 2. 启动 GUI 进行动态分析

在 MATLAB 命令行中进入 `uestcradar` 目录，输入以下命令即可启动可视化工具：

```matlab
radar_gui
```

在弹出的界面中：
1. 点击**“选择发射...”**按钮，选择 TX 目录下的 `.bin` 参考文件。
2. 点击**“选择接收...”**按钮，选择 RX 目录（包含 `cpi_*.bin` 文件的文件夹）。
3. 界面会自动加载两边的 `metadata.json` 并在左侧展示参数。
4. 点击**“播放”**即可在右侧查看无 MTI 与弱 MTI 的 Range-Doppler 谱图及切片对比。
5. 你也可以点击 **“导出 GIF”** 将回放过程保存为动图。

### 3. 数据解析与导出

如果你只需要将 `.bin` 二进制文件解析并提取为 MATLAB 的 `.mat` 矩阵变量以便自己做其它算法验证，请运行：

```matlab
parse_bin
```
按照弹窗提示选择发射和接收文件后，它会自动在接收目录下生成 `tx_*.mat`, `rx_*.mat` 和 `rf_param_*.mat`。

## 许可证

本项目采用 [MIT License](LICENSE) 开源许可证。
