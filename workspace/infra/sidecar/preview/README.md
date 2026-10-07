# Preview 性能验收

Preview 线程与 TCP 通道不属于数据面。Forwarder 只把完整 `RawFrame` 的只读视图交给
`FrameTap::try_capture()`；未订阅时只执行一次原子读取，订阅命中时复制到每个 Leg
独占的单槽缓冲区。缓冲区忙时立即丢弃，不等待 RingBuffer、UCX 或 Worker。

## 自动化门禁

`preview-test` 覆盖：

- 4 通道、19.5 MiB IQ 帧压缩后小于原帧的 1/64；
- IQ 和脉压逐通道极值池化不串道；
- RD 8×8 极值池化保留 `channel_index`；
- 非法或截断帧拒绝编码。

Go 与前端测试另行覆盖 TCP 到 `/ws/frames` 的原样路由、多节点多 Leg 订阅、每路
最新帧覆盖及 TypeScript 多通道解码。

## ARM RDMA 实机 A/B 门禁

同一套 Source→Operator→Sink 拓扑、相同镜像和 Ring 参数下各运行至少 60 秒：

1. 基线组不打开节点详情抽屉，因此 Sidecar 采样率为 0。
2. 预览组打开一个节点详情抽屉，同时订阅输入和输出，各请求 15 fps。
3. 比较稳定区间的端到端 Goodput、Forwarder 帧数、Ring 水位和 CPU/内存带宽。

验收条件：数据帧和字节数无额外丢失，Ring 不因 Preview 新增 Warning，Goodput
中位数退化不超过 1%。`snapshot_drops`、`encode_drops` 或 `network_drops` 可以增长，
它们表示 Preview 主动牺牲帧率保护数据面；任何数据面等待或反向背压均判定失败。
