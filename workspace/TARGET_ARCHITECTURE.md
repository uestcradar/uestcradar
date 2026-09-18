# UESTC Radar 目标架构与理想结果

## 1. 文档目的

本文档定义 UESTC Radar 项目的长期目标、理想架构、核心能力边界和最终验收标准，作为后续架构设计、功能开发、性能优化和版本评审的统一基线。

项目的最终目标是搭建一套：

> **分布式、超高性能、主数据面零拷贝、载荷无关、算子完全解耦，并具备现代可视化能力的微服务雷达信号流式处理系统。**

该系统不应局限于某一种雷达算法或某三种固定数据结构，而应成为可承载信号采集、预处理、脉冲压缩、波束形成、距离多普勒处理、CFAR、点迹凝聚等任意流式算子的通用基础设施。

---

## 2. 总体愿景

理想系统由 Web、Worker、Sidecar 三层组成：

```text
                         ┌──────────────────────────────┐
                         │ Web 可视化与集群控制层       │
                         │ 拓扑、链路、告警、数据可视化 │
                         └──────────────┬───────────────┘
                                        │ 旁路遥测与抽样数据
                                        │ 不回压主数据面

   ┌────────────────────────────────────▼────────────────────────────────────┐
   │                       分布式流式处理集群                                 │
   │                                                                          │
   │  Source Node          Operator Node              Sink Node               │
   │                                                                          │
   │  Worker Source        Worker Algorithm           Worker Sink             │
   │       │                      ▲   │                     ▲                  │
   │       ▼                      │   ▼                     │                  │
   │  Output SHM       Input SHM ┘   └ Output SHM     Input SHM              │
   │       │                ▲              │               ▲                  │
   │       ▼                │              ▼               │                  │
   │  Sidecar Egress ═══ UCX/RDMA ═══ Sidecar ═══ UCX/RDMA ═══ Sidecar      │
   │                                  Ingress/Egress                          │
   └──────────────────────────────────────────────────────────────────────────┘
```

系统必须允许将任意 Worker 组合成跨进程、跨容器、跨物理机的有向流式处理链路，同时保证算法代码不依赖 UCX、RDMA、共享内存、重连或背压实现。

---

## 3. 核心架构原则

### 3.1 数据面与控制面分离

- 主数据面只负责 Frame 的低延迟、高吞吐传输。
- 遥测、告警、数据抽样和 Web 控制属于旁路控制面。
- Web、Telemetry 或 Sampler 故障不得阻塞、暂停或降低主链路的正确性。
- 旁路数据允许限速、丢弃和降采样，不得形成反向背压。

### 3.2 Sidecar 载荷无关

- Sidecar 不解析 IQ、脉压、RD、点云或业务结构体。
- Sidecar 只理解通用 Frame Envelope、Payload 长度、契约标识和传输控制信息。
- 业务解码必须位于 Worker 或独立 Sampler/Decoder 插件中。

### 3.3 Worker 与传输底座完全解耦

- Worker 只通过 SDK 的 `Input<Frame>` 和 `Output<Frame>` 使用数据。
- Worker 不感知对端 IP、端口、UCX endpoint、RDMA 网卡或 credit 协议。
- Worker 可以随时启动、停止、重启或升级；Sidecar 可以独立先行启动。
- Worker 与 Sidecar 通过显式命名的共享内存端口连接，不依赖固定容器名称。

### 3.4 主数据面零拷贝

- Worker 直接在共享内存 Slot 中读取输入 Payload。
- Worker 直接在共享内存 Slot 中构造输出 Payload。
- Sidecar 直接使用已注册的共享内存地址进行 UCX Send/Receive。
- UCX 请求完成前必须保持 Slot lease，禁止提前复用。
- 少量固定大小的 Envelope/Metadata 复制可以接受，但大块 Payload 不得经过临时 `vector`、中转缓冲区或重复 `memcpy`。
- Web 抽样属于旁路能力，允许按低频率进行有界复制，不将其计入主数据面零拷贝承诺。

### 3.5 一切性能结论必须可测量

- “零拷贝”“超高性能”“低于 1 微秒”必须由可重复 benchmark 证明。
- 性能报告必须记录 CPU、NUMA、内核、UCX 版本、网卡、链路速率、Payload、Slot 数、并发窗口和 CPU 占用。
- TCP 功能测试不能替代真实 RDMA/DMA 零拷贝验收。

---

## 4. 第一级：Web 可视化控制层

### 4.1 定位

Web Tier 是通用流式集群的状态仪表盘、拓扑观察器、告警中心和数据帧可视化平台，不直接进入主数据传输链路。

### 4.2 链路连接监控

Web 应实时展示每个 Sidecar 的 upstream/downstream 通道状态：

- `disabled`、`listening`、`connecting`、`connected`、`retrying`、`degraded`、`failed`。
- 本地与远端节点、IP、端口、建连角色和数据路径模式。
- 最近连接时间、断线时间、重试次数和最后错误。
- RTT、吞吐、消息率、字节率、在途请求、credit 等待时间和错误计数。
- UCX transport、RDMA 设备和实际启用的协议路径。

Web 应能够根据遥测自动生成真实的集群拓扑图，而不是依赖硬编码的固定三级节点。

### 4.3 RingBuffer 水位与背压告警

Web 应展示任意 Worker 端口对应 RingBuffer 的：

- Slot 总数、已用 Slot、可用 Slot和实时水位。
- 读写位置、生产/消费速率及水位历史曲线。
- 空环等待、满环等待、背压持续时间和背压事件次数。
- Warning/Critical 阈值、持续时间规则和恢复状态。
- 节点离线、Ring shutdown、契约不匹配和消费者停滞告警。

告警不能只表现为前端颜色变化，应形成明确的告警事件、状态和恢复记录。

### 4.4 通用数据帧可视化

系统应提供低开销的 Data Sampler/Exporter：

1. 在主链路安全持有 Frame lease 时按配置进行抽样。
2. 将抽样帧送入独立、有界、可丢弃的旁路队列。
3. 根据 `type_id`、`type_version` 和 `schema_id` 选择解码插件。
4. 将降采样结果通过二进制 WebSocket 或适当的结构化协议推送到浏览器。
5. 支持波形图、频谱图、二维热力图、三维点云和大数据表。

Sampler 不得作为 RingBuffer 的第二个非安全消费者，也不得因浏览器处理缓慢而回压 Worker 或 Sidecar。

### 4.5 Web 控制能力

在具备认证、授权和审计的前提下，最终可支持：

- 调整抽样率、可视化通道和告警阈值。
- 查询节点配置与版本。
- 启停非关键采样任务。
- 下发经过校验的运行参数。

所有控制操作必须与主数据面隔离，并具备权限控制、审计记录和失败回滚能力。

---

## 5. 第二级：Generic Worker 算法算子层

### 5.1 定位

Worker Tier 是通用业务算子承载层。Worker 可以是任意用户编写的 C++ 算法、GPU 算子、Qt 多线程程序或其他受支持语言的处理进程。

### 5.2 通用 Frame 模型

SDK 应提供稳定的通用 Frame Envelope，至少包含：

- `frame_id`
- `timestamp`
- `type_id`
- `type_version`
- `schema_id`
- `payload_length`
- 可选 flags、trace ID 和校验信息

Payload 必须是载荷无关的连续二进制区域，其上限由端口配置决定，而不是由 SDK 内硬编码的数据类型决定。

SDK 应同时支持：

- `RawFrame`：任意二进制 Payload。
- Typed Frame Adapter：IQ、脉压、RD 等强类型零拷贝视图。
- 用户自定义 Codec/Schema：允许项目外部扩展新的 Frame 类型。

“任意大小”指在端口启动时配置的最大 Payload 范围内支持可变长度 Frame；超出上限时必须显式拒绝，不能截断或静默覆盖。

### 5.3 Worker SDK 接口

理想接口保持简单：

```cpp
Input<Frame> input("input-port");
Output<Frame> output("output-port");

for (;;) {
    auto in = input.read();
    auto out = output.create(metadata, payload_size);
    process(in, out);
    output.write(out);
}
```

SDK 负责：

- SHM 端口发现和等待。
- Frame lease 生命周期。
- 类型、版本、长度和维度校验。
- 满环/空环等待策略。
- 取消未提交输出和释放已消费输入。
- 可配置的低延迟自旋、事件通知或混合等待策略。

### 5.4 本地性能目标

- 大块 Payload 读写不得发生额外复制。
- 持续流量下，Worker 与 Sidecar 的共享内存 Slot 交接目标为 `< 1 us`。
- 空闲唤醒、满环背压和高负载场景应分别给出 P50/P99 数据，不能只报告平均值。
- 支持 CPU 绑核、NUMA 对齐、Huge Page 或其他目标硬件所需优化。

---

## 6. 第三级：Sidecar 旁路网关层

### 6.1 定位

Sidecar Tier 是载荷无关的高性能二进制网关，负责连接本地 Worker SHM 端口与远端 UCX/RDMA 通道。

理想的中间算子节点数据流为：

```text
Upstream Sidecar
       │ Payload + control
       ▼
Ingress UCX Channel
       │ zero-copy receive
       ▼
Worker Input Ring
       │
       ▼
Worker Algorithm
       │
       ▼
Worker Output Ring
       │ zero-copy send
       ▼
Egress UCX Channel
       │ Payload + control
       ▼
Downstream Sidecar
```

Sidecar 不应默认绕过 Worker 将输入直接转发到输出。若未来需要纯 Relay 节点，应以独立、显式的 Relay 模式实现。

### 6.2 单进程双通道串联

单个 Sidecar 进程必须拥有两套相互独立的网络 Leg：

- **Upstream/Ingress Leg**：从上游接收 Payload，写入 Worker Input Ring，并向上游返回 credit。
- **Downstream/Egress Leg**：从 Worker Output Ring 读取 Payload，等待下游 credit 后发送。

每个 Leg 必须可以独立配置：

- `enabled/disabled`
- `listen/connect`
- bind host、peer host、port
- connect timeout、retry backoff
- functional 或 strict-RDMA 数据路径
- Frame 契约和最大 Payload

两个 Leg 必须拥有独立的生命周期、状态、错误和重连逻辑。一个方向断线不得导致另一个方向、SHM 或 Telemetry 被无条件销毁。

### 6.3 节点角色

通过通道悬空支持三类节点：

| 节点类型 | Upstream Leg | Downstream Leg | 数据行为 |
|---|---|---|---|
| Source | Disabled | Enabled | Worker 产生数据并向下游发送 |
| Operator | Enabled | Enabled | Worker 消费输入、计算并产生输出 |
| Sink | Enabled | Disabled | Worker 消费最终结果 |

### 6.4 UCX 与 DMA 内存直连

- RingBuffer Payload 页应通过 `ucp_mem_map` 注册。
- UCX Send/Receive 直接使用 Ring Slot 地址。
- strict-RDMA 模式必须确认实际使用 RC 和零拷贝 Rendezvous 路径。
- 不满足 RDMA 设备、驱动、memlock 或路由条件时应明确启动失败，禁止悄然退化后仍宣称 DMA 零拷贝。
- 网络断线后应释放或重建相关请求、endpoint 和 memory registration，同时保持进程可恢复。

### 6.5 Credit 背压与并发泵送

- 接收方只有在获得可写 Slot 后才向发送方发放 credit。
- 发送方只有在持有 credit 时才发送对应 Payload。
- credit、Payload 和 Slot lease 的生命周期必须严格对应。
- 支持可配置的多 credit/多请求窗口，以提高大带宽高 RTT 链路利用率。
- 必须统计 credit 等待、Ring 等待、网络等待和在途请求数量。

### 6.6 契约校验

每条单向通道在传输 Payload 前必须校验：

- 协议版本。
- Frame `type_id/type_version/schema_id`。
- 最大 Payload。
- 必要的 ABI、端序和能力标志。

契约不匹配应产生清晰错误和 Telemetry 事件，不得接收后再尝试解释不兼容 Payload。

---

## 7. 部署与生命周期理想状态

- Sidecar 可以在没有 Worker 时独立启动、创建 SHM、启动 Telemetry 并建立远端连接。
- Worker 随后启动即可连接已有 SHM，开始收发数据。
- Worker 可以独立重启，不要求远端 Sidecar 或 Web 同步重启。
- Web/Sampler 可以任意离线或重启，不影响主数据链路。
- 网络短暂断开后，Sidecar 自动重连并恢复通道状态。
- Sidecar 重启必须有明确的 SHM 所有权和代际机制，避免旧 Worker 映射与新 Ring 形成 split-brain。
- 同一主机可通过显式端口 ID 和 SHM 名称运行多个 Worker/Sidecar 实例，不发生命名冲突。
- 最终应支持 Compose 和 Kubernetes 部署，并提供健康检查、资源限制、CPU/NUMA 绑定及优雅退出策略。

---

## 8. 最终理想结果

项目完成后，用户应能够：

1. 编写一个只依赖 SDK 的算法 Worker，不接触 UCX、RDMA、共享内存和网络配置。
2. 使用配置将多个 Worker 部署到不同物理机，并组合成任意长度的流式处理链。
3. 在每个节点先启动 Sidecar，待算法容器随时上线后自动接入。
4. 让大块雷达 Payload 从 Worker SHM 直接进入 UCX/RDMA，并在远端直接落入目标 SHM Slot。
5. 在消费者变慢时自动形成可观察、不会覆盖数据的 credit 背压。
6. 在 Web 中看到真实拓扑、链路状态、RTT、吞吐、水位、背压和错误。
7. 点击任意支持的端口，以可控抽样率查看真实波形、频谱、热力图、点云或数据表。
8. 在 Web 或 Sampler 故障时继续稳定处理主数据。
9. 通过标准 benchmark 报告证明共享内存延迟、网络吞吐、P99 延迟和 CPU 占用满足目标。
10. 在节点、Worker 或网络局部故障后实现明确、可观测且可恢复的运行行为。

最终产品不只是一个雷达 Demo，而是一套可以长期承载多种雷达算法和高吞吐二进制数据流的通用微服务运行底座。

---

## 9. 最终验收清单

### 9.1 功能验收

- [ ] 单 Sidecar 同时运行独立 upstream 与 downstream UCX Leg。
- [ ] 两个 Leg 均可独立选择 listen、connect 或 disabled。
- [ ] Source、Operator、Sink 三种节点均有自动化测试。
- [ ] 至少完成三物理节点 A → B → C 级联处理测试。
- [ ] Worker 晚启动、重启和异常退出均有确定行为。
- [ ] 网络断线重连不要求重启整个处理链。
- [ ] 支持 RawFrame 和外部自定义 Frame Codec。
- [ ] 契约不匹配能在传输前拒绝并产生告警。
- [ ] Web 展示真实链路、水位、背压和节点状态。
- [ ] Sampler 展示来自真实 Frame 的波形和二维矩阵。

### 9.2 性能验收

- [ ] SHM Slot 交接延迟达到约定的 P50/P99 指标。
- [ ] 持续流量下本地 Worker/Sidecar 交互目标 `< 1 us` 得到证明。
- [ ] 在目标 RDMA 硬件上证明实际 UCX 零拷贝路径。
- [ ] 端到端吞吐达到目标网卡和 CPU 条件下的约定比例。
- [ ] 背压、突发流量和慢消费者场景无覆盖、乱序或损坏。
- [ ] Sampler 开启和关闭时主数据面性能变化处于约定范围。

### 9.3 工程化验收

- [ ] Ring、SDK、Transport、Forwarder、Telemetry 和 Web 纳入 CI。
- [ ] 保存可追溯的性能报告和测试环境元数据。
- [ ] 配置命名、协议版本和 SHM ABI 有兼容策略。
- [ ] 具备日志、指标、健康检查和故障注入测试。
- [ ] Compose/Kubernetes 部署文档与实际实现一致。
- [ ] 所有“零拷贝”和“高性能”声明都有对应测试证据。

---

## 10. 推荐建设顺序

1. **拓扑正确性**：完成双 Leg Sidecar、单向契约、通道悬空和三节点级联。
2. **通用数据契约**：引入 Frame Envelope、RawFrame 和 Codec/Schema 注册机制。
3. **完整可观测性**：补齐 UCX 状态、重试、RTT、吞吐、背压和告警模型。
4. **真实数据可视化**：实现有界抽样、Decoder 插件和二进制 WebSocket。
5. **性能与可靠性**：完成多 credit window、RDMA 验收、故障恢复和长期压力测试。
6. **生产工程化**：引入 CI、Kubernetes、权限审计、版本兼容和发布流程。

以上顺序优先保证架构和数据语义正确，再进行性能扩展和产品化建设。
