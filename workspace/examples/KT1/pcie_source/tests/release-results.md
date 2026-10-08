# RawIQFrame 发布与硬件首检

## 发布完成

用户授权后，代码提交并推送至 `upstream/feat/signalsink`：

```text
fdb78ccbef00ea87604175a75574142c849b7f8e
```

远端 `/root/workspace/uestcradar` 在确认干净后切至该提交的 detached HEAD，保留原 `feature/agile` 分支。全部镜像由 `.64` 原生 ARM 上的正式发布脚本构建，先检查不可变 Tag 未占用，再推送、拉回校验，最后更新相应滚动 Tag。没有发布 Web、旧 SignalSource 或算法镜像。

| 交付物 | 不可变 Tag（registry.chengyistudio.com/cxx/ 下） | Digest |
| --- | --- | --- |
| SDK | `algo-base:sha-fdb78ccbef00-arm64` | `sha256:7a314aabea8f51e492081d88e9191714f834d73e69556463e18a4104c3d21a42` |
| 仿真 Source | `worker:signalsource-raw-iq-sha-fdb78ccbef00-arm64` | `sha256:b6e1a0505bb2579aa8ad8e4f438cd7738979bbe38d98ad34fa895801a1ea23c1` |
| PCIe Source | `worker:pcie-source-sha-fdb78ccbef00-arm64` | `sha256:5cf489a8efd621ee1a1ce5f54a6e33ee8a16d0eca114e40c49d90a00fc1a330b` |
| SignalSink | `worker:signalsink-sha-fdb78ccbef00-arm64` | `sha256:ecde0bd4d3763852cdc4a140f12197c2736e5348e579f779327391e96e786c46` |

三个 Worker 均使用上述 SDK digest 构建。SDK 首次推送连接超时；按相同流程重试后成功，未修改网络设置，也未覆盖已存在的不可变 Tag。原始构建/发布输出见 `evidence/raw-iq/release/`（归档仅规范化日志行末空白）。

## 发布镜像仿真落盘通过

复用已发布 Sidecar：

```text
registry.chengyistudio.com/cxx/sidecar@sha256:d400a3b523fd1868b21d4a479552ef9de6bfec4ead6940dabae611a052f4f20c
```

服务器原生运行固定 digest 的仿真 Source → 两个 Sidecar → SignalSink，先录制后启动 Source。使用显式 TCP；不是 Web strict-RDMA 验收。Docker 19.03 仅通过 Compose override 去掉 platform 参数，镜像架构仍验证为 arm64。

- 无候选二进制或 SDK 挂载；只有 Sink 捕获目录 bind mount。
- 64 帧、524288 个单通道样本、2102784 裸帧字节；全部 IQ/TX/RX、帧序与正常 footer 校验通过。
- IQ 修改、时间戳修改及截断均拒绝；Sidecar/Sink 容器在录制期间未重启。
- 文件保存在 `/root/workspace/captures/raw-iq-published-fdb78cc/smoke/`，ext4。测试容器结束后已删除，文件保留。
- 证据：`published-pipeline.json`、`published-pipeline.log`。脚本的 `scope` 固定保留旧保守提示；本次是否有二进制挂载以该 JSON 的 `runtime` 和以上实际发布身份为准。

这仍是低速、小文件仿真，不能证明硬件连续性或持续磁盘带宽。

## 真实 PCIe 10 秒首检未通过：没有 IQ/控制数据

2026-10-08 10:37:27～10:37:38 UTC，使用已发布 PCIe digest，通道 0、`--capture-only --duration-seconds 10`。仅映射 `/dev/mem` 并增加 SYS_RAWIO，采用既有板卡初始化配置；未修改配置文件、硬件地址或固件。运行前未发现其他 `/dev/mem` 映射，板卡为 `04:00.0 / 10ee:7038`。

| 项目 | 结果 |
| --- | ---: |
| 单调时钟接收窗口 | 10.000053529 秒 |
| 描述字 / BIT 包 | 10 / 10 |
| IQ 包 / 控制包 | **0 / 0** |
| 单通道样本 / 完整帧 | **0 / 0** |
| 时间戳比较 | 0 |
| 非法包 / 双快照差异 | 0 / 0 |
| 进程退出码 | **2：数据不足或检查未通过** |
| OOM / 崩溃 | 未观察到 |

零错误计数不算通过：本次根本没有 IQ/时间戳可比较。不能据此判断是采集端未送数、链路状态，还是其他硬件条件；需要现场确认。未伪造时间戳/零样本，未跳过检查，未启动后续 10 秒端到端录制及 60 秒录制。

证据：`hardware-10s.log`、`hardware-state.txt`、`hardware-timestamps.jsonl`。服务器诊断原件位于 `/root/workspace/captures/raw-iq-hardware-fdb78cc/`。诊断容器已退出并删除；原 `uestcradar-web` 仍运行，未执行其更新或重启。

## 下一步

1. 确认采集端已启用，光纤链路正在向 PCIe 接收端发送 IQ 和对应控制表。
2. 使用同一固定 digest 重做 10 秒接收首检；有真实数据后才能确认 control→8192 点的配对。
3. 首检通过后，执行 10 秒、60 秒单通道 Source→Sidecar→SignalSink 录制，核对 Source 计数/业务指纹、文件和最终同步；另做真实断链/背压检查。

发布完成不等于硬件验收完成。当前仍不宣称采样连续、DMA 所有权已证明或长期磁盘带宽达标。
