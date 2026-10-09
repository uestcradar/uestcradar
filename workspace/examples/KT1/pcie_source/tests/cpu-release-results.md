# CPU 角色绑定修复：正式发布与双机数据流验证

日期：2026-10-09 UTC。用户已授权发布正式镜像和测试数据流。

## 结论

**正式镜像的正常 Web 双机数据流验证通过，未复现此前的采集配对错误。**

`.64` PCIe Source → strict-RDMA → `.80` SignalSink。完成一个约 10 秒窗口、一个约 60 秒窗口，再完成两次约 66–68 秒的完整 Source 会话。后两次从第 1 帧开始保存，Source 最终帧数/样本数/业务 FNV 与文件的独立 SDK 解码校验一致，Docker die 事件确认 Source 退出 0。

不是临时二进制挂载、不是同机 TCP，不修改 SignalSink/SDK/Sidecar。当前证据是有限时长验证，不是永久无损或无限持续录制承诺。

## 正式交付身份

| 组件 | 提交 / 不可变 Tag | Harbor digest |
|---|---|---|
| Source | `f674cc29ee37bf86facd517873f06cff7a2000d3` / `worker:pcie-source-sha-f674cc29ee37-arm64` | `sha256:6e9b3da9aa6cd0960cdf3e81b68e4f368ea85a797fda15622eeae61be16b0f59` |
| Web | `aa29acd02cc6a08d8e6cfe5aa12231caccea405a` / `web:sha-aa29acd02cc6-arm64` | `sha256:57fb023e6d07ed242a485910430058b4952ad0d094517be384e00911d54a1b41` |

仓库前缀均为 `registry.chengyistudio.com/cxx/`。沿用固定 SDK `algo-base@sha256:7a314aabea8f51e492081d88e9191714f834d73e69556463e18a4104c3d21a42`，没有升级 SDK ABI。

通过官方 release.sh，在原生 ARM 上构建、校验、推送不可变 Tag、拉回校验，再更新滚动 Tag。Source 原工作树中旧诊断改动未混入：发布从本次提交的干净临时 Git worktree 执行，远端也是干净匹配提交。Web 后端全量测试、vet、race 通过。

`.64` 新 Web 容器：`a8c5e5c15bf442d03465e501adcda79cfd48eb15eb678f1e8a4699117272f96f`。保留原网络、环境、UID 65532 和只读 TLS 挂载；旧 Web 容器为 `uestcradar-web-rollback-aa29acd`，此前回退镜像也未删除。

运行中 Source Image ID：`sha256:037a74f95d83a440e07388afac4b4456123e6d1903bd0702cb4d8adf05eb7fe5`，Config.Image 是上述正式 digest，Mounts=[]，没有 `/app/pcie_source` 替换挂载。日志与 `/proc` 同时确认采集 CPU8、DMA CPU9、SDK 输出 CPU10，由程序自己设置，测试脚本没有 taskset。

## 编排及检查方式

- 镜像拉取、计划、部署、录制 start/status/stop 和部署停止，均通过实际 Web API。
- SSH 只用于镜像/线程/日志观测及离线文件校验。沿用已验证指纹；密码/Cookie/CSRF 不进入证据文件，测试后删除会话。
- 两端使用 hns_1，数据地址 `192.170.2.64` / `192.170.2.80`；计划 `DATA_PATH=strict-rdma`，实时遥测报告 `transport=rdma`、connected、非 stale。
- Source 保留 `/dev/mem:rw`、SYS_RAWIO、restart=no；SignalSink 无额外设备/能力。数据保存在 `.80`。
- 整段会话利用 Web 的下游先启动顺序，明确通过录制 API 在 Source 首帧前开启记录；先通过 Web 停止 `.64`，等待 `.80` 接收计数对齐，再停止录制、等待 idle/同步，最后停止 `.80`。不把正在同步的 Sink 直接容器强停。

## 结果

| 会话 | 完整帧 | 样本数 | 原始帧字节 | 判定 |
|---|---:|---:|---:|---|
| 约 10 秒窗口 | 40,925 | 335,257,600 | 1,344,631,800 | 文件结构/顺序/RX 差值/footer 通过 |
| 约 60 秒窗口 | 229,254 | 1,878,048,768 | 7,532,369,424 | 同上，最终 idle/queue=0 |
| 整段 1：Source 67.801 秒 | 254,155 | 2,082,037,760 | 8,350,516,680 | Source 与 SDK 文件校验一致，退出 0 |
| 整段 2：Source 65.761 秒 | 246,505 | 2,019,368,960 | 8,099,168,280 | Source 与 SDK 文件校验一致，退出 0 |

Web 控制和日志采样有延迟，故窗口不是恰好 10.000/60.000 秒；整段实际持续时间取 Source 最终日志，不把控制 API 的等待时间当作采样时间。

第一组 Source 在末次观测已连续运行约 151 秒，invalid_packets/changed_copies/timestamp_errors 均为 0；后两次 Source 最终三项也均为 0，无配对/输出队列故障。整段 1 启动未对齐样本为 8192、停止残帧 0；整段 2 两者均为 0，未伪造或补齐。

整段业务摘要（诊断 FNV-1a64，非密码学证明）：

- 整段 1：Source 与 SDK checker 均为 `7524953354314128364`。
- 整段 2：Source 与 SDK checker 均为 `3345240607619294742`。

独立 SDK checker 从正式提交源码原生编译，SHA256 为 `bb6bcc980c5aa0becf68bc8509f0019a711d1191fa039835388d571ce0294fe3`，离线、只读挂载记录文件，在原 SignalSink 运行镜像环境中执行，不替换运行中的 Worker。它通过 SDK 解码 4:1，并核对从第 1 帧至末帧、footer、帧数及预期业务摘要。另有通用结构检查确认 1×8192、RX 原始差值 98304、TX 全 F 原样保留。

注意：通用结构检查器的固定 `source_digest_comparison` 字段表示该检查器自身不做源摘要比较；整段摘要比对的依据是 `source-final.json`、`sdk-check.json` 与 `result.json`，不是该固定字段。

## 文件保留

均在 `192.162.2.80:/root/workspace/captures/`：

```text
pcie-cpu-published-flow-20261009T014304Z/record-10/fcee76b5231cc36759ad63931ff13151.sink
pcie-cpu-published-flow-20261009T014304Z/record-60/d5a875717ea093fe236e55328dfc9607.sink
pcie-cpu-full-session-20261009T015025Z/capture/2557a43784ab43598804f5bdb5e988db.sink
pcie-cpu-full-session-20261009T015447Z/capture/3e73f4056841b662fefbfd73a8ba3abc.sink
```

原始帧 SHA256（不含 USINK001 长度前缀/footer）：

- 10 秒窗口：`001a522c768f2a5e60e78b26c7b7cb5d40b2e49031c0d12c2990b0b93f0e5946`
- 60 秒窗口：`c3e72f5b91c4467d554b02b7b86419774dd8472b782961dfeb6765a3a5af5ac9`
- 整段 1：`bab02a3c6c16819fea27a1ea2f7a3d7e4ffaaf3691f526f4670aceb6679f2ee9`
- 整段 2：`f8f677f48a8bdfdb9ada92a5f6d7995ca0d707916319b7f8516698d70ce22e46`

## 剩余边界与清理

- 三个一分钟级录制，观测 stop 返回到 idle 仍约 56.0 / 58.9 / 59.4 秒，含轮询误差；不等于纯 fsync 耗时，也不意味着稳态磁盘带宽满足长期输入。
- 采样队列峰值分别约 131,456 / 32,864 / 13,671,424 字节；不是所有瞬间的绝对峰值，均未报告队列溢出。
- `.80` 最终可用空间约 8,039,522,304 字节。没有自动删除任何真实录制；需规划空间后再做长期测试。
- 候选阶段曾出现的启动无输入后 invalid descriptor 原因仍未证实；正式阶段此次未重现。DMA 所有权、硬件不可见漏采和极端调度负载仍未获得保证，sample_continuity 保持 unverified。
- 本次测试部署已通过 Web 停止，新 Web 保持运行，旧 Web可回退。离线校验器临时文件移除；两个 Docker event 观测进程在核对 PID/容器 ID 后结束，/dev/mem 无残留映射。会话已注销。

证据：`tests/evidence/raw-iq/cpu-release/`。`windows/`、`session-1/`、`session-2/` 保留原始计划、镜像身份、控制状态、Source 日志、退出事件及文件检查结果。`check_evidence.py` 校验这些结果与 `sha256.json`。
