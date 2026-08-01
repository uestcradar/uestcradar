# Sidecar 双 Leg 网关

Sidecar 是载荷无关的 C++ 数据网关。单个进程拥有两个彼此独立的网络 Leg 和
两个 fixed-slot SPSC 共享内存 Ring：

```text
上游 Sidecar -> UCX -> Upstream Leg -> Input SHM -> Worker
Worker -> Output SHM -> Downstream Leg -> UCX -> 下游 Sidecar
```

中间算子节点不再需要两个 Sidecar 容器。Upstream 只负责入站，Downstream
只负责出站；每条 Leg 可独立 `disabled`、`listen` 或 `connect`，因此同一二进制
原生支持 Source、Operator 和 Sink。

## 模块边界

- `main.cpp`：组合根。解析配置，创建两块 SHM，启动 Telemetry 和两个 Leg
  线程，分别管理建链、重试和 UCX 内存注册。
- `forwarder/`：连接 Ring 与 UCX 的单向数据泵；不创建 endpoint 或 SHM，
  不解析 Payload。
- `network/`：UCX UCP Tag 传输和 `ucp_mem_map` 封装；不依赖 RingBuffer。
- `telemetry/`：只读采样两块 Ring 的水位，不进入主数据路径。
- `../common/ringbuf/`：Sidecar 与 Worker SDK 共享的 SHM ABI。

基础设施模块 `network` 与 `ringbuf` 禁止互相依赖。所有资源所有权仍集中在
`main.cpp`，算法逻辑只允许出现在 Worker 或 `tools/` 测试程序中。

## Leg 配置

Sidecar 启动时总会创建两块 SHM 并启动 Telemetry。Worker 可以稍后启动，通过
SDK 自动等待并挂载。网络配置如下：

| 配置 | 含义 |
| --- | --- |
| `SIDECAR_UPSTREAM_ROLE` | `disabled` / `listen` / `connect`，默认 `listen` |
| `SIDECAR_DOWNSTREAM_ROLE` | `disabled` / `listen` / `connect`，默认 `disabled` |
| `SIDECAR_<LEG>_BIND_HOST` | listen 地址，默认 `0.0.0.0` |
| `SIDECAR_<LEG>_PEER_HOST` | connect 地址，默认 `127.0.0.1` |
| `SIDECAR_<LEG>_PEER_NODE_ID` | 遥测拓扑中的对端节点 ID |
| `SIDECAR_<LEG>_PORT` | 独立端口；Upstream 默认 13337，Downstream 默认 13338 |
| `SIDECAR_<LEG>_CONNECT_TIMEOUT_MS` | 单次建链超时，默认 2000 ms |
| `SIDECAR_<LEG>_DATA_PATH` | `functional` 强制 TCP；`strict-rdma` 强制 RC/RDMA |
| `SIDECAR_<LEG>_SHM_NAME` | Sidecar 创建的显式 SHM 名称 |

这里 `<LEG>` 是 `UPSTREAM` 或 `DOWNSTREAM`。每块 Ring 另有：

- `SIDECAR_<LEG>_SLOT_COUNT`
- `SIDECAR_<LEG>_MAX_PAYLOAD_BYTES`
- `SIDECAR_<LEG>_TYPE_ID`
- `SIDECAR_<LEG>_TYPE_VERSION`

Worker SDK 使用 `UESTCRADAR_UPSTREAM_SHM_NAME` 和
`UESTCRADAR_DOWNSTREAM_SHM_NAME`；Compose 必须把它们设为对应 Sidecar SHM
名称。旧的 `SIDECAR_UCX_*` 含义含混，现已被明确拒绝，而不是静默映射。

拓扑配置：

| 节点 | Upstream | Downstream |
| --- | --- | --- |
| Source | disabled | listen/connect |
| Operator | listen/connect | listen/connect |
| Sink | listen/connect | disabled |

listen/connect 只决定谁发起连接，不改变数据方向。

## 实时语义与背压

建链后双方交换单端口契约：Producer 的 `type_id/type_version` 必须与 Consumer
一致，且 Producer 最大帧长不能超过 Consumer Slot。每条连接最多一个 Credit
和一个 Payload 在途。Consumer 只有成功预留 Input Slot 后才发 Credit，所以
Ring 满会自然逐级背压；Payload 直接收发于已经注册的 Ring Slot。

系统采用实时流语义，不增加应用层 ACK、重传、去重或历史帧恢复：

- 入站连接失败时取消尚未提交的 Input Slot。
- 出站连接失败时释放当前读租约，并丢弃断链期间积压的 Output 帧。
- 单 Leg 建链或重连失败只重试该线程，不销毁另一 Leg、SHM、Telemetry 或进程。
- 恢复连接后只传新产生的实时帧。

TCP/UCX 负责连接存续期内的可靠字节传输。上述策略有意避免雷达历史数据在
恢复后形成无意义的突发积压。

## 链路遥测

Sidecar 每 100ms 从独立线程旁路读取两个 Leg 的连接原子状态、累计 Payload 字节和
Ring 头。节点 Offline 只由中央 Collector 的 3 秒心跳租约决定；Sidecar 在线但
Leg 未连接时只上报 `disconnected`。Ring 水位超过 70% 由 Collector 判为节点
Warning，Leg 状态不参与节点 Offline 判定。

## 零拷贝边界

Worker 与 Sidecar 之间通过 Ring Slot 就地读写。UCX Payload Send/Receive 也
直接指向同一映射，并传入 `ucp_mem_map` 得到的 memh，不创建中间 Payload
数组。`functional` 模式强制 TCP，仅证明功能；只有 `strict-rdma` 在可用 RC
设备、锁页权限和 UCX zcopy/rendezvous 协议都验证后，才能把结果标记为 RDMA
DMA 零拷贝。

## 构建与测试

```bash
cmake -S workspace/sidecar -B build/sidecar -DBUILD_TESTING=ON
cmake --build build/sidecar --parallel
ctest --test-dir build/sidecar --output-on-failure

workspace/sidecar/tools/run_cascade_test.sh correctness
workspace/sidecar/tools/run_cascade_test.sh benchmark
```

本地三节点 Compose 与测试参数见 [tools/README.md](tools/README.md)，三台 RDMA
主机计划见 [tools/DISTRIBUTED_CASCADE_TEST.md](tools/DISTRIBUTED_CASCADE_TEST.md)。
