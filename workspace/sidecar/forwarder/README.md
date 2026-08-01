# Forwarder

Forwarder 是唯一同时使用 RingBuffer 数据 API 和 UCXTransport 数据 API 的
应用层模块。它提供两个方向明确的会话：

- `run_ingress_session`：网络 Consumer。预留 Upstream Input Slot、投递 UCX
  Receive、发送一个 Credit，收到合法 RawFrame 后提交 Slot。
- `run_egress_session`：网络 Producer。读取 Downstream Output Slot、等待一个
  Credit，直接从该 Slot 发送 RawFrame，完成后释放 Slot。

建链时双方交换协议版本、端口角色和一个
`type_id/type_version/max_payload_bytes` 契约。Producer/Consumer 角色必须相反，
类型必须相同，Consumer 容量必须足够。协议固定为单 Credit 窗口，不包含应用层
ACK、重传、去重或多帧窗口。

异常销毁 Ingress Pump 会 cancel 未提交的写租约；异常销毁 Egress Pump 会释放
当前读租约。组合根在 Egress 重连前调用 `drop_stale_frames` 清空断链积压，恢复
后只继续实时流。Forwarder 本身不创建 SHM、endpoint、线程或重连策略。

每个 Session 接收一个 `LegMetrics`。握手成功到 Session 销毁期间发布
`connected=true`，Payload 完成时以 relaxed 原子操作累计字节。该对象不包含锁、
回调、网络导出或 RTT 探针。

协议版本为 v3。RawFrame Send/Receive 的地址从 64B Envelope 开始，覆盖紧随其后
的有效 Payload，并带入 Ring 映射对应的 `UCXMemoryRegion`。Egress 发送前和
Ingress 发布前都会检查逐帧 `type_id/type_version/payload_length`。因此代码路径
没有临时 Payload buffer 或 memcpy；实际是否使用 NIC DMA 仍须用 strict-rdma
和 UCX 协议信息在目标硬件上验证。

```bash
cmake -S workspace/sidecar -B build/sidecar -DBUILD_TESTING=ON
cmake --build build/sidecar --parallel
ctest --test-dir build/sidecar --output-on-failure
```
