# PulseCompression SignalSink

本工程独立消费`PulseCompressionFrame(2:2)`，在20480±8距离门内进行门控SNR
目标校验，并按64个脉冲汇总一个CPI。它不依赖也不属于SignalSource工程。

## 构建与发布

```bash
docker build --platform linux/arm64 \
  -t registry.chengyistudio.com/cxx/worker:pcsink-latest .
docker push registry.chengyistudio.com/cxx/worker:pcsink-latest
```

Docker构建会先运行`signalsink-target-validation`单元测试。发布后应将远端摘要
固定到上级`docker-compose-infra.yaml`，避免`latest`漂移。

## 运行参数

```bash
signalsink --frames 640 --log-every 64 \
  --workers 4 --queue-depth 8 \
  --target-range 20480 --target-half-width 8 \
  --target-min-snr-db 10 --pulses-per-cpi 64 \
  --fail-on-target-miss
```

真实64脉冲Metadata会产生`target_summary`；反量化占位Worker的
`pulses_per_cpi=1`在非严格模式下输出`target_validation=SKIPPED`。严格模式下，
Metadata不兼容、脉冲索引断序、非有限数据和目标漏检均返回非零退出码。

`--workers`控制并行目标检测线程数，`--queue-depth`控制有界输入缓冲；队列深度
不得小于线程数。默认值分别为4和8，结果始终按接收顺序汇总，不会跳帧。

## 性能基准

镜像包含不依赖Sidecar的纯内核基准程序。以下命令使用真实的22196距离单元帧宽，
预热2秒并测量10秒：

```bash
docker run --rm --entrypoint /app/pcsink-benchmark \
  registry.chengyistudio.com/cxx/worker:pcsink-latest \
  --threads 4 --queue-depth 8 --bins 22196 \
  --warmup-seconds 2 --seconds 10
```

输出中的`payload_mb_s`按十进制MB/s计算，只统计完整执行目标检测的
`ComplexFloat32` Payload。
