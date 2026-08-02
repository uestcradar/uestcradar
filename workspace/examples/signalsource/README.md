# 完整 CPI IQ v3 数据源与示例 Sink

`signalsource` 从一个 CPI 数据目录读取 `metadata.json`、四个逐脉冲参数文件和
little-endian CS16 `input.bin`。一个 `IQFrame` 对应一段完整连续 CPI 回波；程序不会按
`pulse_n` 均分样点。默认数据目录是 `/data/CPI0`，默认重复发送同一份 CPI。

```bash
signalsource --data-dir /data/CPI0 --rate-hz 30 --frames 0
```

数据源在启动时验证：

- `input.bin` 大小严格等于 `sample_count * 4`；
- `pulse_time.txt`、`pulse_phase.txt`、`pulse_freq.txt`、`wd0.txt` 各有
  `pulse_n` 个值；
- `pulse_n` 不超过 IQ v3 固定上限 64。

每个 CPI 只调用一次 `Output<IQFrame>::create(metadata)`，随后将全部 CS16 字节写入
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
  -v /path/to/CPI0:/data/CPI0:ro \
  registry.chengyistudio.com/cxx/signalsource:latest \
  --data-dir /data/CPI0 --frames 1
```

SDK/Ring 集成测试可使用权威 CPI 数据：

```bash
cmake -S src -B build \
  -DCPI0_TEST_DATA_DIR=/path/to/CPI0
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
