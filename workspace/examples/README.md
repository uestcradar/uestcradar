# UESTC Radar SDK 教学示例

算法开发者从下面两个连续数据流开始即可。示例优先保证代码直白、日志可见和结果
可验证，不用于性能压测。

| 示例 | 输入 | 输出 | 主要学习内容 |
| --- | --- | --- | --- |
| [algorithm](./algorithm) | `IQFrameView` | `PulseCompressionFrameView` | 标准 C++ Worker 的 `read/create/write` 生命周期 |
| [qt5-algorithm](./qt5-algorithm) | `PulseCompressionFrameView` | `RDFrameView` | QtCore Worker、跨帧 CPI 和 RD 输出 |

两条链路都由 [signalsource](./signalsource) 提供确定性输入和结果 Sink，并使用当前
Dual-Leg Sidecar、RawFrame ABI、Contract v2 与 Docker Compose 2.4。

## SDK 心智模型

```text
Input<RawFrame>::read()
        ↓
data.h View::from()
        ↓
执行算法
        ↓
Output<RawFrame>::create()
        ↓
data.h View::initialize()
        ↓
Output<RawFrame>::write(std::move(frame))
```

`helloworld`、`qt5core` 和 `cascade_worker` 仍作为底座或专项验证示例保留，但不属于
算法 SDK 的第一条学习路径。
