# PulseCompression SignalSink

本工程独立消费`PulseCompressionFrame(2:2)`，在20480±8距离门内进行门控SNR
目标校验，并按64个脉冲汇总一个CPI。它不依赖也不属于SignalSource工程。

## 构建与发布

```bash
docker build --platform linux/arm64 \
  -t registry.chengyistudio.com/cxx/worker:signalsink-latest .
docker push registry.chengyistudio.com/cxx/worker:signalsink-latest
```

Docker构建会先运行`signalsink-target-validation`单元测试。发布后应将远端摘要
固定到上级`docker-compose-infra.yaml`，避免`latest`漂移。

## 运行参数

```bash
signalsink --frames 640 --log-every 64 \
  --target-range 20480 --target-half-width 8 \
  --target-min-snr-db 10 --pulses-per-cpi 64 \
  --fail-on-target-miss
```

真实64脉冲Metadata会产生`target_summary`；反量化占位Worker的
`pulses_per_cpi=1`在非严格模式下输出`target_validation=SKIPPED`。严格模式下，
Metadata不兼容、脉冲索引断序、非有限数据和目标漏检均返回非零退出码。
