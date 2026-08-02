# PulseCompression 算法开发基座

本目录可以整体复制到任意开发目录后独立使用

`docker-compose-infra.yaml` 提供黑盒测试基础设施；`docker-compose-worker.yaml` 只负责构建和

启动开发者自己的 PulseCompression Worker。两者必须分开启动。

示例算法仅将完整 CPI 的 CS16 按 `dequantization_scale` 转成 ComplexFloat32，用来证明
SignalSource 输入正确。它不是脉冲压缩算法。

## 修改边界

### 禁止修改

以下文件是测试基础设施契约，复制目录后也不要修改：

```text
docker-compose-infra.yaml
```

无论怎样重写算法，还必须遵守以下接口约束：

- Worker 只能使用 SDK 的 `Input<IQFrame>` 和 `Output<PulseCompressionFrame>`。具体的帧契约结构的详细说明，请参考 [SDK 接口指南](../../sdk/README.md)。
- 每个输出帧的 `payload_length` 不得超过 **32 MiB（33,554,432 字节）**，该长度包含Metadata 和矩阵数据。
- `Dockerfile` 构建必须使用官方基座镜像 `registry.chengyistudio.com/cxx/algo-base`，否则无法包含链接并编译 SDK 库。
- `Dockerfile` 底部四个镜像契约 Label 不得删除或修改类型（用于控制台自动识别拓扑与遥测）。
- `docker-compose-worker.yaml` 里面有关共享内存/基座镜像还有 platform 等配置禁止修改。

### 可以自由修改或删除

除上面的基础设施文件和接口约束外，本目录中的其余开发内容都可以自由调整。

## 五步开发流程

### 第一步：复制目录并启动黑盒基础设施

```bash
cp -a /path/to/uestcradar/workspace/examples/pulsecompression \
  ~/my-pulsecompression
cd ~/my-pulsecompression
```

基础镜像是 ARM64。ARM64 主机可直接运行；x86_64 主机先确认已注册 QEMU：

```bash
test -r /proc/sys/fs/binfmt_misc/qemu-aarch64
```

如未注册，只需在主机执行一次：

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm64
```

启动测试基座：

```bash
docker compose -f docker-compose-infra.yaml up -d
```

不要在这个 Compose 命令中加入 Worker 文件。

### 第二步：编写算法

关于 SDK 输入输出管道（`Input<IQFrame>`、`Output<PulseCompressionFrame>`）及各类帧契约结构的详细说明，请参考 [SDK 接口指南](../../sdk/README.md)。

示例入口展示了 Worker 必需的三个动作：

```cpp
auto iq = input.read();
//初始化本次脉压输出的一些基本参数
    PulseCompressionMetadata metadata{
        .channel_count = 1,          // 1. 接收天线/数据通道数量 (本次测试数据源通道数为1,但实机将会接入多通道雷达)
        .range_bin_count = 1024,     // 2. 一维距离门/采样单元数量 (对应脉压矩阵的列数 N_obs)
        .pulse_index = 0,            // 3. 当前脉冲在 CPI 积累周期内的索引序号 (0 ~ pulses_per_cpi - 1)
        .pulses_per_cpi = 8,         // 4. 一个 CPI 积累周期包含的总脉冲数 (对应脉压矩阵的行数 N_pulse)
        .range_resolution_m = 1.5,   // 5. 距离维分辨率 (物理单位：米 m)
    };

auto pulse = output.create(metadata, iq);
process(iq, pulse);  // 替换为真实脉压处理。

output.write(std::move(pulse));
```

`IQFrame` 包含一个完整连续 CPI。真实算法应根据`pulse_time_offset_s` 和采样率确定每个脉冲窗口。

当前示例输出为 6,009,672 字节。输出矩阵尺寸变化时，开发者必须自行保证每个输出帧仍不超过 32 MiB。

### 第三步：构建算法开发的ARM系统镜像

```bash
docker compose -f docker-compose-worker.yaml build
```

生成的开发镜像为：

```text
uestcradar/pulsecompression:dev
```

该命令不会重建或重启黑盒基础设施。

### 第四步：在ARM系统镜像中启动/调试自己的算法

前台启动 Worker，直接查看算法系统镜像的输出日志：

```bash
docker compose -f docker-compose-worker.yaml up
```

另一个终端可以查看基座中的数据源和数据校验器的日志，验证数据流是否正常：

```bash
docker compose -f docker-compose-infra.yaml logs -f signalsource signalsink
```

通用 SignalSink 持续打印接收到的 PulseCompressionFrame 尺寸和峰值。示例代码还会校验CPI0–CPI9 的 IQ Metadata、64 元素脉冲参数、CS16 数据和循环顺序；SignalSink 可用于确认输出帧持续到达。

修改代码后只需重新执行：

```bash
docker compose -f docker-compose-worker.yaml up --build
```

停止算法：

```bash
docker compose -f docker-compose-worker.yaml down
```

全部调试结束后再停止基础设施：

```bash
docker compose -f docker-compose-infra.yaml down
```

QEMU 环境只用于功能和正确性验证，不作为吞吐性能基准。

### 第五步：发布正式镜像

开发完成后，给自己开发完成的镜像打上tag，推送到镜像源即可

```bash
docker tag my-pulsecompression:dev registry.chengyistudio.com/cxx/worker:pulsecompression-v1.0.0

docker push registry.chengyistudio.com/cxx/worker:pulsecompression-v1.0.0
```
