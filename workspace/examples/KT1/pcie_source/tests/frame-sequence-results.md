# frame_sequence 连续性实测（旧协议历史记录）

> 当前协议已改为 TX/RX 两个 64 位时间戳，不再有 frame_sequence。以下数据仅描述当时的运行；对应旧检查开关已删除，不应将这些数值按新布局重新解释，亦不能代替当前 RX 差值 98304 的复测。

[验收记录](hardware-results.md) · [控制协议](../pcie/control-protocol.md)

## 结论

**接收端观察到的控制表 frame_sequence 不连续，有向前跳号，无重复和回退。** 这不是对硬件发端丢帧、光纤丢帧或 IQ 采样缺口根因的判定；只能确定接收端读取到的序号序列存在缺号。

用户要求先跳过 T08H，本次继续使用目标机临时 Docker 诊断镜像，未作为 Harbor 发布验收。诊断仅统计，不因序号变化丢弃 IQ 或清空软件 CPI。

## 运行条件

- 主机：`192.162.2.64`，ARM64。
- 镜像：`pcie-source:sequence-check`。
- image ID：`sha256:ed6b13efa6e6dc1e6a8ac33fc8ac349285ae9e18d6f46ad5bb0a55f1308f8551`。
- channel 0，30 秒，无下游 Worker/Sidecar。
- 每个接收控制包都统计，不是每秒日志抽样；取同步描述字第一根光纤（pipe 0）载荷的 W1，字节偏移 8。
- 只保存前 16 个非连续转换样例，计数覆盖全部观察；无逐包刷日志造成的额外高频 I/O。

```bash
docker run --rm --name pcie-source-capture \
  --device /dev/mem:/dev/mem --cap-add SYS_RAWIO \
  pcie-source:sequence-check --capture-only --channel 0 \
  --duration-seconds 30 --check-frame-sequence
```

## 结果

| 指标 | 数值 |
| --- | --- |
| 控制序号观察数 | 71063 |
| 第一/最后序号 | 6987711 / 7100039 |
| 严格 `+1` 转换 | 46790 |
| 重复 | 0 |
| 向前跳号 | 24272 |
| 回退 / `UINT32_MAX → 0` | 0 / 0 |
| 向前跳号累计缺失序号值 | 41266，不直接称为 IQ 丢包数 |
| CTRL_HEAD 与 W0 与文档相符次数 | 71063 / 71063，仅旁路观察，不作为采集放行条件 |
| IQ 包数 | 132804 |
| 完成软件 CPI | 724 |
| invalid_packets / changed_copies | 0 / 0 |
| 退出时丢弃尾块 | 92040 点 |
| 运行中因控制序号清空部分 CPI | 0（此行为已禁用） |

算术核对：`71063 - 1 = 46790 + 24272`；`7100039 - 6987711 = 71062 + 41266`。

样例：

```text
6987814 → 6987816  delta=2
6987818 → 6987821  delta=3
6987823 → 6987826  delta=3
6987885 → 6987890  delta=5
```

日志中的 `strict_continuous=false` 才是本次连续性结论。进程退出码 `0` 仍只表示有 IQ、完成定长软件分块且未触发原有接收错误，不能拿它代替序号连续性通过。`integrity_verified=false` 保持不变。

[完整原始日志](evidence/capture-sequence-check-channel0.txt) · [ARM64 构建日志](evidence/docker-build-sequence-check.txt)

本地 CTest 10/10、ARM64 镜像内 CTest 9/9 通过；序号分类单测覆盖连续、重复、跳号、回退、回绕及样例缓冲上限。运行后容器退出，无残留 `/dev/mem` 占用。

## 解释边界

观察结果与文档表头、W0 匹配，支持本次偏移 8 读取的是 frame_sequence，而非误读 N/通道掩码。仍不能由此断言：缺号是在发端、FPGA 缓冲、PCIe 描述字队列，还是用户态读取/覆盖过程中产生。未修改寄存器配置、未恢复控制序号驱动的丢弃策略，后续定位需独立证据。
