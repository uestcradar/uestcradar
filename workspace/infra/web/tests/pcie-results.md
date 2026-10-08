# Web PCIe 部署修复与双机验证

日期：2026-10-08 UTC。代码提交 `6f254ed5c70b62ca4e91cc9da9d3183932e261c3`。

## 结论

**Web 设备配置及更新完成；长期采集稳定性未通过。** 全程使用正常 Web API 编排 `.64` PCIe Source → strict-RDMA → `.80` SignalSink，不是同机 TCP。未修改 Source/Sink/SDK/Sidecar，不实施 CPU 绑定或全局系统调优。

第二轮约 10 秒的录制窗口完整、同步完成且结构检查通过；随后 Source 在运行 **18.566757791 秒**时退出 1：

```text
control arrived before the previous IQ frame was complete
```

因此 **60 秒录制未开始**。不能把部署修复或短文件校验当作采集稳定性修复。该错误在本次真实双机拓扑也出现；本次没有调度追踪，不能直接把此前同机的 CPU 争用诊断提升为双机根因结论。

## 发布与回退

- ARM 原生构建，官方 release.sh Web 路径发布、拉回验证，再更新 latest。
- 镜像：`registry.chengyistudio.com/cxx/web:sha-6f254ed5c70b-arm64`
- 固定引用：`registry.chengyistudio.com/cxx/web@sha256:0c05762105d24cc93165f502a72048e83f358ef36b96a0b78103928f6869ffd2`
- 新容器 ID：`b4b900c7938df5316d734d55a95f35b3216076a2d7f08cc8c81a584554cf8865`
- 旧容器保留为 `uestcradar-web-rollback-6f254ed`，未删除旧镜像。
- 保留 UID 65532、host 网络、原环境和 TLS 只读挂载。HTTPS 首页正常；已使用新 SignalSink start/status/stop API，旧 404 已消除。更新使旧内存 SSH 会话失效。

本地及 ARM：Go 全量测试、vet、orchestration race 通过；前端 29/29 与构建通过。新增 PCIe 测试先在旧实现失败。首次 ARM 测试脚本遗漏生成 telemetrypb，导致 setup failed；按 Dockerfile 执行 protoc 后全量重跑通过，失败及成功日志均保留。

## 正常编排与权限

通过 Web 执行镜像同步、节点探查、计划预览、部署及录制操作；SSH 仅用于只读运行状态、日志和文件核验。沿用已验证主机指纹，凭据及 Cookie 未写入证据，结束后删除会话。

- `.64` Source digest：`5cf489a8efd621ee1a1ce5f54a6e33ee8a16d0eca114e40c49d90a00fc1a330b`。
- `.80` Sink digest：`ecde0bd4d3763852cdc4a140f12197c2736e5348e579f779327391e96e786c46`。
- 两侧 hns_1，RDMA 地址 `192.170.2.64` / `192.170.2.80`。
- 计划 `DATA_PATH=strict-rdma`、`UCX_TLS=rc_verbs,tcp`；这是现有严格 RDMA 配置，不是 functional/tcp,self 模式。保留的链路遥测报告 `transport=rdma`。
- PCIe Worker 实际映射 `/dev/mem`、CgroupPermissions=`rw`、CapAdd=`SYS_RAWIO`、RestartPolicy=`no`。
- SignalSink/Frontend 无设备映射与附加能力；Sidecar 仅保留原有 InfiniBand/IPC_LOCK。
- 文件保存节点为 `.80` 的 `/root/workspace/captures/`，不是 `.64`。

停止后的最后遥测（已标记 stale/offline，非实时运行状态）：两侧累计 payload 均为 2,278,584,912 字节，Ring 读写位置均为 69,486，与 Source 完整帧计数一致。此计数核对不是源数据全字节摘要比对。

## 录制证据

第二轮文件：

```text
192.162.2.80:/root/workspace/captures/web-pcie-crossnode-20261008T153307Z/record-10/63aaabc502d012e495bd7d019623600c.sink
```

| 项目 | 结果 |
|---|---|
| written / accepted | 40,868 / 40,868 |
| 样本数 | 334,790,656 |
| 原始帧字节 | 1,342,759,008 |
| 帧 ID | 20,333 .. 61,200，连续 |
| 契约 / 矩阵 | RawIQFrame 4:1 / 1×8192 CS16 |
| RX 相邻差值 | 98,304，全部符合 |
| 首帧 TX | 18446744073709551615，原值保留 |
| 收尾状态 | idle，queue=0，正常 footer |
| 原始帧 SHA256 | `cff3564aadc91c37680ae06380433554ac95b29feb522a28a4b964d450d609bb` |

校验脚本检查 USINK001、完整 Envelope/Metadata/Payload 长度、类型、形状、帧顺序、RX 差值和 footer；脚本作为诊断证据附带，不代替已有 SDK 解析/FNV 正式验收。录制从运行中的流开启，未取得同一窗口的 Source 独立摘要，不能宣称源/文件全字节一致；硬件连续性仍为 unverified。约 14.163 秒的 recording-to-idle 时间包含控制与最终同步，不是稳态磁盘带宽测量。

首轮另保留 40,898 帧、1,343,744,688 原始字节的文件，Sink 已返回 idle/queue=0；测试脚本误用 `/root/workspace/docker/captures` 导致 FileNotFoundError，故该轮没有完成独立文件校验。原错误保留，不改写为通过。真实文件：

```text
192.162.2.80:/root/workspace/captures/web-pcie-crossnode-20261008T153044Z/record-10/b3fc4c1a34ba01f8ad39e85594b46c8d.sink
```

## 清理与剩余工作

两轮测试均通过 Web 停止本次两节点部署；录制文件未删除，新 Web 保持运行，旧 Web 可回退。不绕过故障继续做 60 秒，不自动绑定线程或修改 Source。

后续须单独处理 Source 的采集配对错误，再重做 60 秒及更长时间稳定性、独立源/文件摘要和存储能力验收。

原始证据位于 [`evidence/pcie/`](evidence/pcie/)，`sha256.json` 为文件索引。`first-window/` 保留脚本路径错误，`source-failure/` 保留第二轮的真实 Source 失败、已保存窗口检查、计划、镜像同步及部署停止任务结果。
