# Qt5 距离多普勒（RDMap）算法开发基座

本目录可以整体复制到任意开发目录后独立使用。它不依赖真实雷达硬件：
`docker-compose-infra.yaml` 启动包含真实 CPI0～CPI9 数据的完整黑盒数据流，
`docker-compose-worker.yaml` 只构建和运行开发者自己的 Qt5 RD Worker。开发链路为
`纯 SignalSource → PulseCompression → RDMap（待开发）→ RD Sink`，一个CPI由
连续的64个标准脉压帧组成。末端Sink消费`RDFrame(3:2)`，与PulseCompression
示例使用的门控SignalSink职责独立。

## 修改边界

> 如果下面的边界不能满足算法需求时，请联系 SDK 维护者提出需求，由维护者统一修改测试基座，以保证上下游的数据布局始终一致。

### 禁止修改

复制目录后，以下黑盒基础设施契约文件禁止修改：

```text
docker-compose-infra.yaml
```

无论如何重构算法工程，还必须保留这些接口边界：

- 输入必须是 `Input<PulseCompressionFrame>`，输出必须是`Output<RDFrame>` 。输入输出的具体帧契约结构的详细说明，请参考 [SDK 接口指南](../../sdk/README.md)
- 输出矩阵的行数使用输入帧中的真实距离门数量，列数固定为 `65` 个多普勒单元。
- 单个输出帧不得超过 **32 MiB（33,554,432 字节）**，该限制包含 32 字节 Metadata 和矩阵数据。
- `Dockerfile` 构建必须使用官方基座镜像 `registry.chengyistudio.com/cxx/algo-base`（或 `qt5-algo-base`），否则无法包含（`#include <data.h>`）及编译 SDK 库。
- `Dockerfile` 底部四个镜像契约 Label 必须保持为 `worker/v2`、`operator`、`2:2`和 `3:2`，不得删除或修改类型。
- `docker-compose-worker.yaml` 中的运行接口和 `/uestcradar_qt5_algorithm_up`、`/uestcradar_qt5_algorithm_down` 两个通道名称不得修改。

### 可以自由修改或删除

除`docker-compose-infra.yaml`文件和接口约束外，算法代码、测试、CMake、Dockerfile 以及`docker-compose-worker.yaml` 的工程组织方式都可自由修改删除。建议把

### 第一步：启动黑盒测试基座

```bash
cp -a /path/to/uestcradar/workspace/examples/qt5-algorithm ~/my-rd-algorithm
cd ~/my-rd-algorithm
docker compose -f docker-compose-infra.yaml up -d
```

不要在这个命令中合并 Worker Compose。基础镜像是 ARM64；x86_64 主机如未注册QEMU，先执行一次：

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm64
```

### 第二步：编写 Qt5 距离多普勒算法

入口是 `src/main.cpp`，算法函数是 `src/my_rd_algorithm.hpp`。入口保留
`QCoreApplication` 和 `qInfo()`，主处理路径只有三步：

```cpp
// 1. 读数据并缓存；连续收齐 64 帧后得到完整 CPI。
auto pulse = input.read();
auto pulse_metadata = pulse.metadata();
cpi.push(pulse_metadata, pulse.data()[0]);
if (!cpi.ready()) continue;

// 2. 显式填写 RDMetadata 并创建一个不超过 32 MiB 的 RDFrame。
RDMetadata metadata{
    .channel_index = 0,
    .range_bin_count = static_cast<std::uint32_t>(cpi.range_bin_count()),
    .doppler_bin_count = 65,
    .range_resolution_m = pulse_metadata.range_resolution_m,
    .velocity_resolution_mps = 0.5,
};
auto rd = output.create(metadata, pulse);

// 3. 计算、查看峰值并提交 RDMap。
auto result = compute_rd(cpi, rd.data());
qInfo() << "RD peak=" << result.peak_range_bin << result.peak_doppler_bin;
output.write(std::move(rd));
cpi.clear();
```

SDK 帧 API 的完整说明见 [SDK 接口指南](../../sdk/README.md)。

### 第三步：构建本地 RD 算法镜像

```bash
docker compose -f docker-compose-worker.yaml build
```

本地开发镜像为 `uestcradar/rd-algorithm:dev`。该命令不会构建或重启黑盒 Infra。

### 第四步：挂载运行并查看 PASS 日志

前台启动 Qt5 Worker：

```bash
docker compose -f docker-compose-worker.yaml up
```

另一个终端查看真实数据流和 QA Sink：

```bash
docker compose -f docker-compose-infra.yaml logs -f signalsource pulsecompression rd-sink
```

结构校验通过时会持续看到：

```text
[PASSED] RDFrame received=<n> shape=<range>x65 peak_range=<r> peak_doppler=<d> magnitude=<v>
```

最新一帧 RDMap 同时保存在 `output/rdmap_result.pgm`。

修改代码后使用可单独重启 Worker，无需重启测试基座。：

```bash
docker compose -f docker-compose-worker.yaml up --build
```

停止时先停 Worker，再停黑盒基础设施：

```bash
docker compose -f docker-compose-worker.yaml down
docker compose -f docker-compose-infra.yaml down
```

### 第五步：发布 RD 算法镜像

发布名称固定为：

```text
registry.chengyistudio.com/cxx/worker:rd-algorithm-v1.0.0
```

执行：

```bash
docker tag uestcradar/rd-algorithm:dev registry.chengyistudio.com/cxx/worker:rd-algorithm-v1.0.0
docker push registry.chengyistudio.com/cxx/worker:rd-algorithm-v1.0.0
```
