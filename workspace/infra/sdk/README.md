# UESTC Radar Worker SDK 6

SDK 6 面向算法开发者只提供两个头文件：

- `data.h`：`IQFrame`、`PulseCompressionFrame`、`RDFrame`、`RawIQFrame` 及其业务 Metadata。
- `sdk.h`：类型化 `Input`、`Output` 的底层声明，以及用于完整帧录制的只读 `RawFrame` / `Input<RawFrame>`；算法处理通常只需包含 `data.h`。

## 非阻塞完整裸帧读取

源码新增接口（尚未完成新的 ARM 镜像发布，旧 algo-base digest 不包含此能力）：

```cpp
#include <sdk.h>

uestcradar::Input<uestcradar::RawFrame> input(3, 2); // 必须匹配实际输入端口
if (auto frame = input.try_read()) {
    const auto bytes = frame->bytes(); // 原始 Envelope + 完整帧体，只读
    // 在此作用域内使用，或复制到有界自有存储；不要保留 bytes 的悬空视图。
}
```

- `try_read()` 不等待新帧，空结果只表示暂无输入；关闭、损坏和契约不匹配明确报错。
- 一个 Input 同时只能持有一个帧；RawFrame 可移动、不可复制，析构释放 lease。
- RawFrame 保持底层映射有效，即使 Input 已销毁；离开帧生命周期后 span 失效。
- 调用者在每轮读取之间检查退出条件，因此退出不依赖上游再发一帧；不改变共享 Ring 的 shutdown。
- 保留原始字节但不解析自定义 Metadata，也不证明硬件采样连续；它用于录制/诊断，不取代算法的标准类型契约。
- 原有类型化接口和 Ring ABI 不变。构建 SignalSink 时必须先安装包含此接口的 SDK，并固定验证过的 SDK 镜像版本。


> 数据格式使用约束：算法开发者必须使用 `data.h` 中已经定义的标准输入输出帧，
> 不得在算法项目中私自声明、复制或修改数据帧格式。现有数据帧不能满足算法需求时，
> 请联系 SDK 维护者，由维护者统一修改 `data.h`、版本化 JSON 契约、类型注册和契约
> 测试，以保证生产者、消费者及跨语言解码端的数据布局始终一致。

## 有界输出等待

新接口需使用包含本次变更的 SDK；旧镜像不自动获得这些符号：

```cpp
#include <data.h>

uestcradar::Output<uestcradar::RawIQFrame> output(std::chrono::seconds(3));
uestcradar::RawIQMetadata metadata{0, 0, 1, 2}; // 仅示例值，不是硬件时间戳
if (auto frame = output.try_create(metadata)) {
    frame->data()[0][0] = {1, -1};
    frame->data()[0][1] = {2, -2};
    output.write(std::move(*frame));
}
```

带超时构造函数同时限制等待 SHM 名称和已初始化头部的时间，负值拒绝，0 表示立即尝试。`try_create()` 仅在 Ring 满时返回空；关闭、损坏、非法维度和仍持有前一帧均抛错。调用者检查退出条件后再重试；空结果不推进帧序号，放弃已创建帧会取消槽租约，但不会回退其序号。帧必须全部填好后才提交。

默认构造函数和原有 `create()/write()` 的阻塞语义不变。底层只新增打开超时重载，不改变 Envelope 或 Ring ABI。`RawIQFrame` 是强类型业务帧，不能与表示完整只读线字节的 `RawFrame` 混淆。

## 拉取镜像后进入算法开发环境

`algo-base` 镜像包含 SDK、g++、make 和 CMake，启动后提供命令行编译环境，
没有 Web 页面，也不会自动运行雷达算法。以下命令在 Linux Bash 中执行；
Docker 引擎需要已启动，主机需支持运行所拉取镜像的架构（ARM64 镜像在
x86_64 主机上运行时需要配置模拟支持）。

### 1. 选择已拉取的镜像

如果拉取的是 `latest`：

```bash
ALGO_IMAGE=registry.chengyistudio.com/cxx/algo-base:latest
```

如果按 digest 拉取，直接使用对应的完整引用，无需额外添加 `latest` 标签。
例如，已拉取以下版本时：

```bash
ALGO_IMAGE=registry.chengyistudio.com/cxx/algo-base@sha256:d63bad75fc9cc8c4f0fe4b8bbee0e4089a6634318ee2be57692b83fd59425487
```

### 2. 启动交互式容器

在设置 `ALGO_IMAGE` 的同一个终端执行：

```bash
docker run --rm -it "$ALGO_IMAGE" bash
```

输入 `exit` 退出。`--rm` 会在退出后删除容器，容器内未挂载到宿主机的修改
不会保留。

### 3. 挂载算法代码并编译

在宿主机进入自己的算法工程目录（包含 `CMakeLists.txt`），再执行：

```bash
cd /path/to/my-algorithm
docker run --rm -it \
  -v "$PWD":/app \
  -w /app \
  "$ALGO_IMAGE" bash
```

进入容器后编译：

```bash
cmake -S . -B build
cmake --build build --parallel
```

`/app` 对应宿主机的算法工程目录，代码修改和 `build/` 中的编译产物会保留。
运行依赖 SDK 输入输出的 Worker 还需要 Sidecar 和共享内存配置，完整开发流程见
[KT2 脉压示例](../../examples/KT2/README.md)或
[KT3 Qt5 RD 示例](../../examples/KT3/README.md)。

## 标准数据类型定义

当前源码定义四种标准数据帧，前三种旧契约保持不变：

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

### 4. `RawIQFrame`（采集 IQ · `type_id=4` / `type_version=1`）

`RawIQMetadata` 仅含 `tx_timestamp:uint64`、`rx_timestamp:uint64`、`channel_count:uint32`、`samples_per_channel:uint32`。时间戳原值保留，不在 SDK 中换算单位。Wire Metadata 共 24 字节；Payload 为小端 CS16 通道行/采样点列矩阵，尺寸动态，两个维度必须非零。

帧不含 CPI、PRT 或波形参数，不隐式转换成 `IQFrame 1:3`。当前 PCIe Source 仅输出单通道；完整字段布局以 [raw_iq.json](contracts/raw_iq.json) 为准。

## 读取数据

```cpp
#include <data.h>

using namespace uestcradar;

Input<IQFrame> input;
auto iq = input.read();
auto metadata = iq.metadata();
auto samples = iq.data(); // 返回 Array2D<ComplexInt16>
```

### 二维数据矩阵索引与访问方式

强类型数据帧统一通过 `frame.data()` 返回 `Array2D<T>` 二维视图对象，索引语法如下：

#### 1. `IQFrame`（数据类型：`ComplexInt16` CS16 复数）
* **索引语法**：`iq.data()[channel][sample_index]`
  - `channel`: 通道号（`0 ~ metadata.channel_count - 1`）
  - `sample_index`: 采样点序号（`0 ~ metadata.samples_per_channel - 1`）
* **代码示例**：
  ```cpp
  auto matrix = iq.data();
  ComplexInt16 sample = matrix[0][1000]; // 0 号通道第 1000 个采样点
  int16_t i_val = sample.i; // I 实部
  int16_t q_val = sample.q; // Q 虚部
  std::span<const ComplexInt16> channel_0 = matrix[0]; // 通道 0 连续数据切片
  ```

#### 2. `PulseCompressionFrame`（数据类型：`ComplexFloat32` 单精度复数）
* **索引语法**：`pulse.data()[pulse_index][range_bin]`
  - `pulse_index`: 脉冲序号/行号（`0 ~ metadata.pulses_per_cpi - 1`）
  - `range_bin`: 距离门序号/列号（`0 ~ metadata.range_bin_count - 1`）
* **代码示例**：
  ```cpp
  auto matrix = pulse.data();
  ComplexFloat32 val = matrix[pulse_idx][bin_idx]; // 第 pulse_idx 脉冲、第 bin_idx 距离门
  float i_val = val.i;
  float q_val = val.q;
  ```

#### 3. `RDFrame`（数据类型：`float` 或 `ComplexFloat32` 幅值/复数）
* **索引语法**：`rd.data()[doppler_bin][range_bin]`
  - `doppler_bin`: 多普勒速度网格行号（`0 ~ metadata.doppler_bin_count - 1`）
  - `range_bin`: 距离门网格列号（`0 ~ metadata.range_bin_count - 1`）
* **代码示例**：
  ```cpp
  auto rd_map = rd.data();
  float power_dB = rd_map[doppler_idx][range_idx]; // 第 doppler_idx 多普勒通道、第 range_idx 距离门能量
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
