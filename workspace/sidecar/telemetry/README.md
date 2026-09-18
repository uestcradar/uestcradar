# Telemetry

本模块是旁路、latest-only 观察者。

- 只认识由组合根注入的快照回调，不包含 RingBuffer、UCX 或 POSIX shared memory
  依赖。
- 监控目标由 `TelemetryTarget` 动态描述；模块不硬编码共享内存名称和链路拓扑。
- 每个采样周期最多发送一个固定大小、非阻塞 UDP Protobuf 数据报。
- DNS 失败在遥测线程内重试；发送或序列化失败只丢弃当前快照。
- 指标仅包含 Leg 连通性、TCP/RDMA、累计 Payload 字节和 Ring Slot 快照，不包含
  RTT 或 Payload 数据。

唯一入口是 `run_telemetry_exporter`。模块不拥有被观察资源，也不向主数据链路
回传确认或控制信息。
