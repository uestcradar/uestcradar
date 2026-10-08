# Channel 0：10 分钟连续采集

镜像 `pcie-source:rx98304-parallel`，image ID `sha256:a06fd8f748b228af151587fdd2d9219b2361b5aa61dcd05e836c16a565e3d2aa`，主机 `192.162.2.64`。capture-only、channel 0、600 秒，无下游。

本次首次启动因同名容器已在运行而报 Docker Conflict（退出 125），未启动第二个设备进程。检查现有容器确认其镜像及参数恰为本轮 600 秒测试，随后等待此轮完成并核对宿主完整日志；没有移除或重启正在采集的容器。

结果：600.000077288 秒，4499802 个 IQ 包、18431188992 个选中通道样本，平均 **30.718644 MS/s**。2249901 个控制表，2249900 次时间戳比较全部差值 **98304**，异常 **0**。无效包和变化副本均 0，完成 24535 个软件 CPI，退出尾块 349782 点；退出码 0，结束后 `/dev/mem` 无占用。

600 条周期/最终采集报告中，相邻报告的 IQ 包计数全部增长，未观察到秒级停流。完整异常日志只有 run_start/run_end 两行，与零异常统计一致。

原始记录留在目标机：

```text
/root/pcie-continuity-600s-x06LQV/capture.txt
/root/pcie-continuity-600s-x06LQV/errors.jsonl
```

仓库副本：[采集日志](evidence/throughput/continuity-600s.txt) · [JSONL](evidence/throughput/continuity-600s.jsonl)。`tests/check_capture.py` 核对日志计数和 30.4128 MS/s 性能门槛通过。

结论：本轮 10 分钟未观察到数据流中断或 RX 时间戳跳变。仍为 channel 0 诊断，不证明八路逐样本正确性或 DMA 所有权，保持 `integrity_verified=false`；不计 Harbor 发布或下游验收通过。
