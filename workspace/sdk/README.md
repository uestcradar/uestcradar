# UESTC Radar Worker SDK 6

SDK 6 面向算法开发者只提供两个头文件：

- `data.h`：`IQFrame`、`PulseCompressionFrame`、`RDFrame` 及其业务 Metadata。
- `sdk.h`：类型化 `Input`、`Output` 的底层声明；通常只需包含 `data.h`。

> 数据格式使用约束：算法开发者必须使用 `data.h` 中已经定义的标准输入输出帧，
> 不得在算法项目中私自声明、复制或修改数据帧格式。现有数据帧不能满足算法需求时，
> 请联系 SDK 维护者，由维护者统一修改 `data.h`、版本化 JSON 契约、类型注册和契约
> 测试，以保证生产者、消费者及跨语言解码端的数据布局始终一致。

## 标准数据类型定义

SDK 6 定义了三种标准雷达数据帧，各自的业务 Metadata 字段与 Payload 内存布局说明如下：

### 1. `IQFrame`（原始 IQ 信号数据帧 · `type_id=1` / `type_version=3`）

一帧承载包含一个完整 CPI 积累周期的原始复数回波及最多 64 组逐脉冲调制参数。

| 字段名 (`IQMetadata`) | 类型 | 说明 |
| :--- | :--- | :--- |
| `cpi_index` | `uint64_t` | CPI 全局自增帧序号 |
| `channel_count` | `uint32_t` | 数据通道数 (例如 1 通道) |
| `samples_per_channel` | `uint32_t` | 单通道原始复数采样点总数 (例如 751,206 点) |
| `pulse_count` | `uint32_t` | 本 CPI 积累周期内的脉冲总数 (例如 64 脉冲) |
| `wave_process_type` | `uint32_t` | 波形处理类型编号 (如捷变频 4) |
| `velocity_oversampling` | `uint32_t` | 速度维过采样率 |
| `sample_rate_hz` | `double` | 采样率 (Hz，如 30.72 MHz) |
| `nominal_carrier_frequency_hz` | `double` | 标称中心载频 (Hz，如 3.0 GHz) |
| `bandwidth_hz` | `double` | 信号带宽 (Hz，如 2.0 MHz) |
| `pulse_width_s` | `double` | 脉冲宽度 (s) |
| `nominal_prt_s` | `double` | 标称脉冲重复周期 PRT (s) |
| `observation_max_range_m` | `double` | 观测最大距离 (m) |
| `dequantization_scale` | `double` | 量化缩放因子 |
| `pulse_time_offset_s[64]` | `std::array<double, 64>` | 64 个脉冲的相对发射时间偏移 $R_t$ (s) |
| `pulse_phase_rad[64]` | `std::array<double, 64>` | 64 个脉冲的初相 $\phi$ (rad) |
| `pulse_frequency_hz[64]` | `std::array<double, 64>` | 64 个脉冲的实际发射载频 $R_f$ (Hz) |
| `coherent_weight[64]` | `std::array<double, 64>` | 64 个脉冲的相干权重 $w_{d0}$ |
| **Payload 数据区** | `ComplexInt16` 矩阵 | 小端 CS16（`int16_t I, int16_t Q`），尺寸为 `channel_count × samples_per_channel` |

---

### 2. `PulseCompressionFrame`（脉冲压缩数据帧 · `type_id=2` / `type_version=2`）

承载一维匹配滤波（脉冲压缩）解算后的距离维数据。

| 字段名 (`PulseCompressionMetadata`) | 类型 | 说明 |
| :--- | :--- | :--- |
| `channel_count` | `uint32_t` | 接收天线/数据通道数量 (通常为 1) |
| `range_bin_count` | `uint32_t` | 一维距离门/采样单元数量 (对应矩阵列数 $N_{\text{obs}}$) |
| `pulse_index` | `uint32_t` | 当前脉冲在 CPI 内的索引序号 (0 ~ `pulses_per_cpi` - 1) |
| `pulses_per_cpi` | `uint32_t` | 一个 CPI 积累周期包含的总脉冲数 (对应矩阵行数 $N_{\text{pulse}}$) |
| `range_resolution_m` | `double` | 距离维物理分辨率 (单位：米 m) |
| **Payload 数据区** | `ComplexFloat32` 矩阵 | 单精度复数（`float i, float q`），尺寸为 `(channel_count × pulses_per_cpi) × range_bin_count` |

---

### 3. `RDFrame`（距离-多普勒图数据帧 · `type_id=3` / `type_version=2`）

承载二维慢时间 FFT 解算后的距离-多普勒（Range-Doppler Map）图谱矩阵。

| 字段名 (`RDMetadata`) | 类型 | 说明 |
| :--- | :--- | :--- |
| `channel_index` | `uint32_t` | 接收通道索引号 (默认 0) |
| `range_bin_count` | `uint32_t` | 距离维采样门数量 (对应矩阵列数) |
| `doppler_bin_count` | `uint32_t` | 多普勒维网格数量 (对应矩阵行数) |
| `reserved` | `uint32_t` | 32 位显式填充对齐字段 |
| `range_resolution_m` | `double` | 距离维物理分辨率 (单位：米 m) |
| `velocity_resolution_mps` | `double` | 速度维物理分辨率 (单位：米/秒 m/s) |
| **Payload 数据区** | `float` 或 `ComplexFloat32` 矩阵 | 单精度浮点幅值或复数，尺寸为 `doppler_bin_count × range_bin_count` |

---

## 读取数据

```cpp
#include <data.h>

using namespace uestcradar;

Input<IQFrame> input;
auto iq = input.read();
auto metadata = iq.metadata();
auto samples = iq.data();
```

## 创建并写出数据

```cpp
Output<PulseCompressionFrame> output;

PulseCompressionMetadata metadata{
    .channel_count = 1,
    .range_bin_count = 1024,
    .pulse_index = 0,
    .pulses_per_cpi = 8,
    .range_resolution_m = 1.5,
};

auto pulse = output.create(metadata, iq);
// 填充 pulse.data()
output.write(std::move(pulse));
```

数据源没有上游输入时使用 `output.create(metadata)`。处理中间结果使用
`output.create(metadata, input_frame)`，SDK 会自动关联输入与输出。未写出的输出帧会
自动放弃，输入帧离开作用域后会自动释放。

SDK 6 继续使用既有 Ring ABI v6 和 Sidecar protocol v3。IQFrame 当前契约为
`type_id=1/type_version=3`，一帧承载完整 CPI 连续回波及最多 64 组逐脉冲参数。
每种帧分别维护 `type_id/type_version`，`create(metadata, parent)` 只有一个通用入口，
新增帧不会产生逐帧组合重载。

## SDK 维护

算法开发者无需了解数据帧的物理布局。需要新增或修改标准数据帧时，请由 SDK
维护者按照[数据帧契约维护指南](contracts/README.md)统一修改并验证。
