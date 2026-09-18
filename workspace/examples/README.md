# UESTC Radar SDK 教学示例

算法开发者从下面两条连续数据流开始即可。示例以代码直白、日志可见和结果可验证为
目标，不用于性能压测。

| 示例 | 输入 | 输出 | 学习内容 |
| --- | --- | --- | --- |
| [pulsecompression](./pulsecompression) | `IQFrame` | `PulseCompressionFrame` | 完整 CPI、精确数据自检和标准 `read/create/write` 流程 |
| [qt5-algorithm](./qt5-algorithm) | `PulseCompressionFrame` | `RDFrame` | QtCore、跨帧 CPI 和 RD 输出 |

```text
Input<DataFrame>::read()
        ↓
填写输出 Metadata
        ↓
Output<DataFrame>::create(metadata, input)
        ↓
执行算法：input.data() → output.data()
        ↓
Output<DataFrame>::write(std::move(output))
```

[signalsource](./signalsource) 为两条教学流提供确定性测试输入和结果校验程序，算法
开发者通过各示例的一键开发环境使用它，无需单独配置。
