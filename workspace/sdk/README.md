# UESTC Radar Worker SDK 4

SDK 4 的传输接口只包含 `RawFrame`、`Input<RawFrame>` 和
`Output<RawFrame>`。`data.h` 提供 IQ、脉压和距离-多普勒的安全零拷贝
View。Worker 不需要包含任何 `common` 内部头文件。

物理帧固定为 `[64B Envelope][payload_length bytes Payload]`。

## 读取 IQ Contract v2

```cpp
#include <data.h>

using namespace uestcradar;

Input<RawFrame> input;
RawFrame raw = input.read();
auto iq = IQFrameView::from(raw);
auto samples = iq.data();
```

`IQFrameView::from()` 检查 type、version、维度、溢出和 Payload 精确长度。样本
矩阵直接引用共享内存，不复制整帧。

## 创建 RawFrame

```cpp
Output<RawFrame> output;
IQMetadata metadata{1, 1024, 2.5e6, 1.2e9};
Envelope envelope{
    .frame_id = 1,
    .timestamp = timestamp_ns,
    .type_id = IQFrameView::type_id,
    .type_version = IQFrameView::type_version,
    .payload_length = static_cast<std::uint32_t>(
        IQFrameView::payload_bytes(metadata)),
};
RawFrame raw = output.create(envelope);
auto iq = IQFrameView::initialize(raw, metadata);
// 填充 iq.data()
output.write(std::move(raw));
```

未 write 的输出帧析构时自动 cancel；输入帧析构时自动 release。一个端口同一
时刻只能持有一个活动 RawFrame。

SDK 4、Ring ABI v6、Sidecar protocol v3 和 Contract v2 必须整套部署，不能与
SDK 3 或旧共享内存混用。
