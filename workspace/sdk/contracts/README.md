# 数据帧契约维护指南

> [!IMPORTANT]
> 本文档面向 SDK 维护者。算法开发者只应使用 `data.h` 中已有的数据帧；现有格式
> 不满足需求时，应向 SDK 维护者提出变更需求，不要在算法项目内自定义传输格式。

## 最短修改流程

新增或修改一种帧时，改动收敛在四处：

1. 在 [`../include/data.h`](../include/data.h) 增加或修改公开的 Metadata 和强类型
   Frame。
2. 增加或修改本目录中的 `<name>.json` 契约。
3. 在 [`../include/contract_catalog.def`](../include/contract_catalog.def) 增加或修改
   一行注册。
4. 增加或修改该帧的一个契约测试。

修改已有帧的字段、字段类型、偏移、矩阵元素类型或矩阵形状时，只提升该帧自己的
`type_version`。新增帧必须分配唯一且非零的 `type_id`；生产者和消费者必须使用完全
一致的 `type_id/type_version`，SDK 不做隐式版本转换。

完成上述修改后，直接执行[验证](#验证)。只有需要设计或排查物理布局时，才需要继续
阅读下面的映射细节。

## 构建期生成物

正常构建 SDK 时，契约生成器会自动读取注册表和 JSON，并生成：

- C++ `ContractTraits`；
- `contracts.manifest.json`；
- Go 解码代码；
- TypeScript `DataView` 解码代码。

生成物安装到 `share/cycomm_sdk/contracts/`。不要手工复制字段偏移，也不要直接修改
生成物。只修改某个现有 JSON 契约时，构建系统只重新编译该契约对象并重新链接 SDK，
其他帧的类型编号和版本保持不变。

## `data.h` 与 JSON 如何对应

### 物理布局

一帧在共享内存和网络中的布局为：

```text
[64B Envelope][定长 Wire Metadata][连续矩阵数据]
               \_______________________________/
                       Envelope Payload
```

Envelope 的 `payload_length` 包含 Wire Metadata 和矩阵数据。JSON 中
`payload.offset` 是矩阵数据相对于 Envelope Payload 起点的偏移，因此契约必须满足：

```text
payload.offset == metadata_size
payload_length == metadata_size + rows * columns * sizeof(element)
```

### 字段对应关系

| JSON 属性 | `data.h` 中的对应内容 | 作用 |
|---|---|---|
| `cpp_frame` | `IQFrame` 等强类型 Frame | 指定 `Input<T>`、`Output<T>` 使用的帧类型 |
| `cpp_metadata` | `IQMetadata` 等 Metadata 结构体 | 指定 `metadata()` 和 `create(metadata)` 使用的类型 |
| `fields[]` | Metadata 的同名成员 | 定义各字段的线格式类型和固定字节偏移 |
| `payload.cpp_element` | `ComplexInt16`、`ComplexFloat32`、`float` | 指定 `frame.data()` 的元素类型 |
| `payload.rows` | Metadata 中代表行数的字段 | 决定 `frame.data().rows()` |
| `payload.columns` | Metadata 中代表列数的字段 | 决定 `frame.data().columns()` |
| `metadata_size` / `payload.offset` | Wire Metadata 与矩阵数据的分界 | 决定 `frame.data()` 的起始位置 |

### IQ 帧示例

`data.h` 中的 C++ 接口：

```cpp
struct IQMetadata {
    std::uint32_t channel_count;
    std::uint32_t samples_per_channel;
    double sample_rate_hz;
    double center_frequency_hz;
};

class IQFrame {
public:
    IQMetadata metadata() const;
    Array2D<ComplexInt16> data();
};
```

对应的 [`iq.json`](iq.json) 关键内容：

```json
{
  "cpp_frame": "IQFrame",
  "cpp_metadata": "IQMetadata",
  "metadata_size": 24,
  "fields": [
    {"name": "channel_count", "type": "uint32", "offset": 0},
    {"name": "samples_per_channel", "type": "uint32", "offset": 4},
    {"name": "sample_rate_hz", "type": "float64", "offset": 8},
    {"name": "center_frequency_hz", "type": "float64", "offset": 16}
  ],
  "payload": {
    "cpp_element": "ComplexInt16",
    "rows": "channel_count",
    "columns": "samples_per_channel",
    "offset": 24
  }
}
```

当 `channel_count=4`、`samples_per_channel=1024` 时，SDK 计算：

```text
矩阵元素数     = 4 * 1024
矩阵字节数     = 4 * 1024 * sizeof(ComplexInt16)
payload_length = 24 + 矩阵字节数
data() 起点    = Envelope Payload 起点 + 24
```

## 为什么不依赖 C++ 内存布局

C++ Metadata 的 `sizeof` 和成员 `offsetof` 不需要与 JSON 相同，也不要使用
`#pragma pack`。生成的 `ContractTraits` 按照 JSON 的固定偏移逐字段编码和解码：

- `data.h` 是算法开发者使用的自然 C++ 类型；
- JSON 是 C++、Go 和 TypeScript 共用的物理线格式事实来源。

因此，编译器填充、平台 ABI 差异不会被直接带入共享内存或网络数据。

## 如何保证 Payload 映射正确

Payload 映射由四层检查保证：

1. 生成器拒绝字段重叠、字段越界、未显式声明的 Metadata Padding，以及不存在的
   `rows` / `columns` 引用。
2. 编译期 `static_assert` 保证 `sizeof(cpp_element)` 与 JSON 元素字节数一致。
3. `create()` 检查行列非零、乘法溢出、总长度溢出及 RingBuffer 最大 Payload。
4. `read()` 根据解码后的 Metadata 重新计算期望长度；只有它与 Envelope
   `payload_length` 完全一致时，SDK 才返回强类型视图。

契约测试还应使用一份黄金字节数据，验证 Metadata 字段偏移、矩阵起点和最终
`payload_length`。

## 验证

在 `workspace/sdk` 目录执行：

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

需要确认没有明显吞吐退化时，再运行数据面基准：

```bash
./build/sdk-benchmark 4096 50000
./build/sdk-benchmark 65536 20000
./build/sdk-benchmark 1048576 3000
```
