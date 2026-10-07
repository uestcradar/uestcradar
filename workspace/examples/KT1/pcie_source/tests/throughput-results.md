# RX 读取吞吐优化与 98304 间隔验收

[验收汇总](hardware-results.md) · [当前协议](../pcie/control-protocol.md)

用户要求优化读取吞吐，并随后明确将时间戳预期间隔改为 **98304**。本轮沿用用户允许的临时目标机 Docker 构建/测试，不计 Harbor 发布验收；所有运行均在 `.64`、channel 0、capture-only，无下游，未改板卡配置、权限或 DMA 映射属性。

## 最终结果

最终镜像 `pcie-source:rx98304-parallel`，ARM64，image ID：

```text
sha256:a06fd8f748b228af151587fdd2d9219b2361b5aa61dcd05e836c16a565e3d2aa
```

30 秒复测：

| 指标 | 结果 |
| --- | --- |
| 时长 | 30.000073298 秒 |
| 所选通道样本数 | 920788992 |
| 平均接收速率 | **30.692891 MS/s**，约标称 30.72 的 99.91% |
| IQ 包数 | 224802 |
| 控制表数 / 时间戳比较数 | 112401 / 112400 |
| 差值为 98304 / 异常 | **112400 / 0** |
| invalid_packets / changed_copies | 0 / 0 |
| 完成软件 CPI | 1225 |
| 运行中丢弃部分 CPI | 0 |
| 退出尾部 | 561642 点 |
| 退出码 | 0 |
| strict_continuous | true |
| integrity_verified / downstream_tested | false / false |

先前 10 秒优化检查同样无时间戳异常（37401 次比较）；之后重新运行旧读取路径仍有 8865 次异常，再运行上述优化版 30 秒得到 0 次异常。这支持旧读取路径消费不及时是之前跳变的重要原因，但不提供 DMA 所有权或逐样本正确性证明。

运行前后 `/dev/mem` 无其他占用，结束后无残留采集容器。统计中的八路样本数按接受的同步描述字长度推导，本轮仅选择 channel 0，不冒充八路已逐路验收。

## 定位与分步对照

基线计时使用单独的临时 `pcie-source:profile-baseline` 镜像，只有阶段计时插桩；最终源码不含该插桩。10 秒中，第一遍 IQ 读取/解码耗时 4.896 秒，第二遍 4.804 秒，合计约 **97%**；描述字读取 0.041 秒、组帧 0.118 秒、异常日志写入 0.053 秒。反汇编显示编译器已将原解码主循环向量化，不能把它简单归因为逐字节访问。

| 版本/实验 | 预期差值 | 时长 | MS/s | 时间戳异常数 | 决策 |
| --- | --- | --- | --- | --- | --- |
| 原版复测 | 8192 | 10 秒 | 19.204 | 23865 | 保存基线；阈值之后按用户要求修改 |
| 原版加计时 | 8192 | 10 秒 | 19.318 | 24088 | 定位 DMA 读取为主瓶颈，插桩不进入正式源码 |
| 仅改阈值，原读取方式 | 98304 | 10 秒 | 19.070 | 8669 | 同阈值性能基线 |
| 每样本独立 4 字节读取 | 98304 | 10 秒 | 11.936 | 9321 | 更慢，撤销 |
| 顺序批量双快照，普通内存解码 | 98304 | 10 秒 | 24.226 | 6868 | 有改善，保留批量快照方向 |
| 固定 128 字节内联拷贝循环 | 98304 | 10 秒 | 21.265 | 8943 | 不优于标准 memcpy，撤销 |
| 两半协作并行双快照 | 98304 | 10 秒 | 30.639 | 0 | 保留 |
| 再次运行原读取方式 | 98304 | 10 秒 | 21.417 | 8865 | 基线仍未跟上，存在运行波动 |
| 并行双快照最终复测 | 98304 | 30 秒 | 30.693 | 0 | 达到本轮吞吐目标 |

实验并非严格 CPU 隔离基准，故不声称精确固定百分比加速。基线观测在 19～21 MS/s，最终达到标称输入速率附近。

## 保留的修改

- `TimestampCheck::expected_delta=98304`，比较、帮助和 JSONL 都引用同一常量。每硬件帧采样点数仍为 8192，原始时间戳不除以 12、不取整。
- 两份可复用的原始快照；主线程与一个常驻协作线程各负责同一包的一半，每半均执行两次有屏障隔开的读取。
- 完成两半后，主线程从普通内存解码两份选中通道并比对。仍逐样本覆盖整个选中通道，不抽样跳过检查。
- 一次只有一个在处理的包，调用返回前协作线程完成；没有裸 DMA 指针排队或跨 `poll()` 的未完成复制。停止时先 join 再解除映射。
- 小于 4096 字节的包仍顺序双复制；日志保留同步缓冲写入，未增加额外日志线程/队列。已达到输入速率，不继续增加零分配接口或 CPU 绑核配置。

两份快照相同依然不能证明没有稳定覆盖。两个半包的复制并发，不构成原子全包快照，也不是硬件 ACK/租约。

## 执行与持久记录

最终实际运行：

```bash
docker run --rm --name pcie-source-capture \
  --device /dev/mem:/dev/mem --cap-add SYS_RAWIO \
  --mount type=bind,src=/root/pcie-optimized-30s-NJ0Sqt,dst=/logs \
  pcie-source:rx98304-parallel --capture-only --channel 0 --duration-seconds 30 \
  --timestamp-errors /logs/errors.jsonl
```

目标机原始文件为 `/root/pcie-optimized-30s-NJ0Sqt/capture.txt`、`errors.jsonl`。无异常，因此 JSONL 只有 run_start/run_end 两行，不是停止了异常记录。

最终证据：

- [采集原文](evidence/throughput/optimized-30s-NJ0Sqt.txt)
- [完整 JSONL，gzip 无损压缩](evidence/throughput/optimized-30s-NJ0Sqt.jsonl.gz)
- [ARM64 构建与 CTest](evidence/docker-build-rx98304-parallel.txt)

其余实验的采集日志和全部异常也保存在 `tests/evidence/throughput/`，文件前缀与下列目标机目录一致（去掉 `pcie-` 前缀，后缀分别 `.txt` / `.jsonl.gz`）：

```text
/root/pcie-speed-baseline-soCz5m
/root/pcie-profile-baseline-DiUZfj
/root/pcie-rx98304-baseline-9VYJNF
/root/pcie-rx98304-wordcopy-JQu53f
/root/pcie-rx98304-snapshot-nE1muD
/root/pcie-rx98304-bulk128-qXdVfV
/root/pcie-rx98304-parallel-nbbu91
/root/pcie-baseline-repeat-XDTKuu
/root/pcie-optimized-30s-NJ0Sqt
```

通过 `check_capture.py` 逐份核对异常条数与摘要、前后值与 uint64 实际差值相符，全部通过；没有抽样删减异常。复查吞吐：

```bash
python3 tests/check_capture.py \
  tests/evidence/throughput/optimized-30s-NJ0Sqt.txt \
  tests/evidence/throughput/optimized-30s-NJ0Sqt.jsonl.gz --min-msps 30.4128
```

原版历史证据运行同一性能门槛会失败，最终证据通过。这个门槛仅用于吞吐，不替代时间戳和样本检查。

## 离线验证与限制

- 本地 CTest **12/12**、UBSan **12/12**；ARM64 镜像内 CTest **11/11**（未配置单独的 signalsource 差分测试）。
- 协作复制单测额外通过 ASan+UBSan；覆盖串行/并行分支、非对齐、尾部、缓冲前后哨兵、反复提交、停止与重复停止。真实接收器模拟测试覆盖全部八路解码、原始快照复用后已返回样本不变。
- 本机 TSan **未运行成功**，启动时报 `FATAL: ThreadSanitizer: unexpected memory mapping 0x5e50c9c73000-0x5e50c9c75000`，不能声称经过 TSan 验证。未修改系统 ASLR 设置绕过。
- 仅短窗口 channel 0 采集验证；长时间、其他通道、可控 IQ 正确性、Harbor 发布及 SDK/Sidecar 输出仍未完成。
