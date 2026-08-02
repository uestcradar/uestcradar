# 完整 CPI IQ v3 数据源与示例 Sink

`signalsource` 启动时从数据根目录依次加载 `CPI0` 至 `CPI9`。每个目录包含
`metadata.json`、四个逐脉冲参数文件和 little-endian CS16 `input.bin`。一个
`IQFrame` 对应一个完整 CPI，不会按 `pulse_n` 均分样点。

```bash
signalsource --data-root /data --frames 0
```

发送顺序为 `CPI0 → … → CPI9 → CPI0 → …`，不使用定时器或人工帧间隔；
吞吐只受下游 Ring 消费速度限制。每个发出帧的 `cpi_index` 从 0 全局递增，
循环回 `CPI0` 时不会重置。兼容旧命令行的 `--data-dir /data/CPI0`，但仍会加载
同级的全部十个 CPI 目录。

数据源在启动时验证：

- `input.bin` 大小严格等于 `sample_count * 4`；
- `pulse_time.txt`、`pulse_phase.txt`、`pulse_freq.txt`、`wd0.txt` 各有
  `pulse_n` 个值；
- `pulse_n` 不超过 IQ v3 固定上限 64。
- `CPI0`–`CPI9` 的 metadata `cpi_index` 必须与目录序号一致；
- 十个 CPI 的矩阵形状必须为 `1 × 751206`，且固定体制参数一致。

每个 CPI 只调用一次 `Output<IQFrame>::create(metadata)`，并使用该 CPI 自己的
64 元素 `pulse_time/pulse_phase/pulse_freq/wd0` 数组，随后将全部 CS16 字节写入
`frame.data().values()` 对应的连续矩阵区域。镜像契约输出为 `1:3`。

同一镜像还包含：

- `frame-sink --type pulse|rd`：消费示例结果；
- `pulse-source`：保留 Qt5 RD 教学链所需的独立脉压测试源，不占用 IQ v3 镜像入口。

## 构建与运行

```bash
docker build \
  -t registry.chengyistudio.com/cxx/signalsource:latest \
  workspace/examples/signalsource

docker run --rm \
  -v /path/to/EchoExport:/data:ro \
  registry.chengyistudio.com/cxx/signalsource:latest \
  --data-root /data --frames 10
```

SDK/Ring 集成测试可使用权威 CPI 数据：

```bash
cmake -S src -B build \
  -DCPI_TEST_DATA_ROOT=/path/to/EchoExport
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
