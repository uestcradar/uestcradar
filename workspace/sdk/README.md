# UESTC Radar Worker SDK 6

SDK 6 面向算法开发者只提供两个头文件：

- `data.h`：`IQFrame`、`PulseCompressionFrame`、`RDFrame` 及其业务 Metadata。
- `sdk.h`：类型化 `Input`、`Output` 的底层声明；通常只需包含 `data.h`。

> 数据格式使用约束：算法开发者必须使用 `data.h` 中已经定义的标准输入输出帧，
> 不得在算法项目中私自声明、复制或修改数据帧格式。现有数据帧不能满足算法需求时，
> 请联系 SDK 维护者，由维护者统一修改 `data.h`、版本化 JSON 契约、类型注册和契约
> 测试，以保证生产者、消费者及跨语言解码端的数据布局始终一致。

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

SDK 6 继续使用既有 Ring ABI v6 和 Sidecar protocol v3，不改变物理数据格式。
每种帧分别维护 `type_id/type_version`，`create(metadata, parent)` 只有一个通用入口，
新增帧不会产生逐帧组合重载。

## SDK 维护

算法开发者无需了解数据帧的物理布局。需要新增或修改标准数据帧时，请由 SDK
维护者按照[数据帧契约维护指南](contracts/README.md)统一修改并验证。
