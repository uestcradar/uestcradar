# 双时间戳协议：首次硬件检查（历史阈值 8192）

> 下文保留当时的失败结果。用户随后指定以 98304 检查相邻时间戳，读取路径也已优化；最新 30 秒结果无异常，见 [性能与复测记录](throughput-results.md)。

[验收汇总](hardware-results.md) · [协议](../pcie/control-protocol.md)

用户明确允许本次临时在 `192.162.2.64` 构建新版 Docker 并独立采集；这是对部署流程的一次例外，不计 T08H Harbor 发布通过。未连接下游、未改板卡配置或时间戳阈值。

## 镜像与命令

- `pcie-source:rx-timestamp`，ARM64。
- image ID：`sha256:a79a8226944a8270be777da60fd77f85deb227af4871e4fea2e7e8eb23da9d0d`。
- 构建使用目标机现有同版模板标签 `registry.chengyistudio.com/cxx/worker:signalsource-cpi0-cpi9-20260802`。
- ARM64 构建 CTest **10/10** 通过（不含本地单独配置的 signalsource 差分测试）。
- 运行前后 `/dev/mem` 均无其他占用，采集结束后无残留容器。

```bash
docker run --rm --name pcie-source-capture \
  --device /dev/mem:/dev/mem --cap-add SYS_RAWIO \
  --mount type=bind,src=/root/pcie-rx-timestamp-eK1B87,dst=/logs \
  pcie-source:rx-timestamp --capture-only --channel 0 --duration-seconds 30 \
  --timestamp-errors /logs/rx_timestamp_errors.jsonl
```

该宿主日志目录由 `mktemp -d` 新建，未覆盖历史记录。

## 实际结果

**数据读取有数据，RX 时间戳差值 8192 的检查不通过。**

| 指标 | 结果 |
| --- | --- |
| 时长 | 30.000035524 秒 |
| IQ 包数 / 四光纤总 IQ 字节数 | 141017 / 18483380224 |
| 控制表观察数 | 71890 |
| 时间戳比较数 | 71889 |
| 差值等于 8192 | 0 |
| 异常数 | 71889 |
| 完成软件 CPI | 768 |
| invalid_packets / changed_copies | 0 / 0 |
| 运行中丢弃部分 CPI | 0 |
| 退出尾部丢弃 | 679424 点 |
| 退出码 | 0，仅表示数据存在性检查通过 |
| strict_continuous | false |
| integrity_verified / downstream_tested | false / false |

全部异常重新用保存的前后值计算 uint64 模差，均与记录中的 delta_u64 相符；异常行数等于最终错误计数。日志共 71891 行：71889 条异常及 run_start/run_end 两条边界记录。

| 实际差值 | 次数 |
| --- | --- |
| 98304 | 45490 |
| 196608 | 15991 |
| 294912 | 7867 |
| 393216 | 1699 |
| 491520 | 621 |
| 589824 | 196 |
| 688128 | 22 |
| 786432 | 1 |
| 884736 | 1 |
| 983040 | 1 |

所有观察差值均为 **98304 的整数倍**，其中 `98304 = 8192 × 12`。第一对 RX 值为 `4038857246424 → 4038857344728`，实际差值 98304；对应控制包计数 `1 → 2`、描述字计数 `2 → 5`、载荷长度均 24 字节、掩码 15。所有异常中的 TX 值均为 `2075264829144`；按约定未用于校验。

**尚未确定倍数关系的原因。** 需核对 RX 时间戳计数域/单位及接收完整性；不能直接把 12 倍差值解释成漏掉 11 帧，也不能据此把阈值改为 98304 后宣布无错。此次有大量异常同步日志写入，未隔离其吞吐影响；也未证明控制载荷不会在读取前被 DMA 覆盖。

## 持久记录

目标机原始文件：

```text
/root/pcie-rx-timestamp-eK1B87/capture.txt
/root/pcie-rx-timestamp-eK1B87/rx_timestamp_errors.jsonl
```

异常文件 35337075 字节，原始 SHA-256：

```text
8866619fa017769ec7522f98d181574d6e230385dc45cbaf3fd177aa705ce389
```

已核对目标机与本地副本哈希一致；全部异常另以无损 gzip 归档，没有抽样删减：

- [完整采集日志](evidence/capture-rx-timestamp-channel0.txt)
- [完整异常 JSONL（gzip）](evidence/rx-timestamp-errors-channel0.jsonl.gz)
- [ARM64 构建日志](evidence/docker-build-rx-timestamp.txt)

本次证明新版程序能读取并保存实际异常，不证明时间戳连续或 IQ 数据完整。
