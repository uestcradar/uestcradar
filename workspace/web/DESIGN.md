# Sidecar Link Telemetry 设计

## 范围

页面只展示动态 Sidecar 拓扑、Leg 连通性、TCP/RDMA、Payload Goodput 和
RingBuffer 最新快照。没有 RTT、Payload 抽样、历史曲线、波形或热力图。

节点与 Leg 使用两套独立状态：

- 节点心跳超过 3 秒才是 `offline`。
- 心跳有效时，任一 Ring 水位大于 70% 为 `warning`，否则为 `normal`。
- Leg 单独为 `connected`、`disconnected` 或 `disabled`。Leg 断开不得把节点改成
  `offline`。
- 节点离线后保留 Leg 最后状态，并在输出中标记 `stale`。

## 数据流与隔离

```text
Forwarder payload completion
  -> relaxed atomic byte counter
RingBuffer header + Leg connected atomic
  -> Sidecar telemetry thread, 10 Hz snapshot
  -> fixed buffer + non-blocking UDP Protobuf
  -> Collector latest-only Store
  -> capacity-1 dirty signal
  -> capacity-1 WebSocket client queue
  -> Browser SVG topology + one detail table
```

数据线程只增加一次 relaxed 原子累加，不调用 Protobuf、UDP、JSON 或 WebSocket。
Collector 和浏览器变慢时覆盖旧遥测快照，不向 Sidecar 发送确认或控制消息。

## 指标语义

- Goodput 是对应 Leg 成功完成的 Payload 字节差分，使用 Collector 接收时间计算十进制
  GB/s，不是物理网卡全局计数器。
- `capacity_slots` 来自 Ring 固定槽位数。
- `used_slots = write_position - read_position`。
- `watermark_pct = used_slots * 100 / capacity_slots`，由 Collector 计算。
- `instance_id` 变化时清空速率基线；同一实例乱序或重复 sequence 直接丢弃。

## 配置

Sidecar：

- `NODE_ID`
- `SIDECAR_INSTANCE_ID`，可选；默认由 node、PID 和启动时间生成
- `SIDECAR_<LEG>_PEER_NODE_ID`
- `TELEMETRY_HOST`、`TELEMETRY_PORT`
- `SAMPLE_INTERVAL`，默认 100ms

Collector：

- `TELEMETRY_UDP_ADDR`，默认 `:9900`
- `TELEMETRY_HTTP_ADDR`，默认 `:8080`

HTTP `GET /api/snapshot` 返回初始完整快照，`GET /ws` 持续推送最新完整快照。
