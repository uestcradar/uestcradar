# UESTC Radar Worker SDK 6

SDK 6 面向算法开发者只提供两个头文件：

- `data.h`：`IQFrame`、`PulseCompressionFrame`、`RDFrame` 及其业务 Metadata。
- `sdk.h`：类型化 `Input`、`Output` 的底层声明；通常只需包含 `data.h`。

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

## 新增或修改一种帧

改动严格限定为四处：

1. 在 `include/data.h` 增加或修改公开的 Metadata 和强类型 Frame。
2. 增加或修改 `contracts/<name>.json` 契约。
3. 在 `include/contract_catalog.def` 增加或修改一行注册。
4. 增加或修改该帧的一个契约测试。

构建期自动生成 C++ `ContractTraits`、`contracts.manifest.json`、Go 解码代码和
TypeScript `DataView` 解码代码。生成物安装在
`share/cycomm_sdk/contracts/`，无需手工同步字段偏移。

只修改某个现有 JSON 契约时，构建系统只重新编译该契约对象并重新链接 SDK；其他
帧的契约对象、类型编号和版本保持不变。Producer 和 Consumer 必须使用完全一致的
`type_id/type_version`，不进行隐式版本转换。

## 验证

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

SDK 数据面基准工具支持指定 Payload 和帧数：

```bash
./build/sdk-benchmark 4096 50000
./build/sdk-benchmark 65536 20000
./build/sdk-benchmark 1048576 3000
```
