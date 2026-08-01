# 教学数据源与接收端

本目录为 SDK 教学示例提供两个基础程序，算法开发者通常不需要修改它们：

- `signalsource --type iq|pulse`：持续产生 IQ 或脉压测试帧。
- `frame-sink --type pulse|rd`：消费结果并打印矩阵尺寸和峰值。

两个程序都只包含 `data.h`，分别展示最小的 `Output<RawFrame>` 和
`Input<RawFrame>` 用法。它们由 `../algorithm/docker-compose.infra.yaml` 和
`../qt5-algorithm/docker-compose.infra.yaml` 自动启动，不再维护第三份重复的
Sidecar Compose。

## 构建

基础镜像必须包含当前 SDK 4、Ring ABI v6 和 Contract v2：

```bash
docker build \
  -t registry.chengyistudio.com/cxx/signalsource:latest \
  workspace/examples/signalsource
```

启动后会看到类似日志：

```text
[source] type=iq rate_hz=20 frames=0
[source] sent type=iq frame=20
[sink] type=pulse received=20 frame=20 shape=1x128 peak_range_bin=0
```
