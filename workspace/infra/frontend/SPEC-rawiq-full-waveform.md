# RawIQ 完整帧波形（已确认范围）

目标：Web 节点页显示 RawIQ 4:1 每个展示帧的全部原始 CS16 点，保持幅度 |I+jQ|、通道选择和最高 30 FPS。完整帧不等于全部采集帧。其他契约继续原有预览。

实现顺序：新增预览 ComplexInt16 编码；Sidecar 按通道发送小端 I/Q 原字节；Go 校验新格式；浏览器按采样索引解码并绘制单条完整幅度曲线；测试、原生 ARM 发布、Web 部署、浏览器验证。

格式：ValueEncoding=5，WaveformChannel.bucket_count 在此编码下表示点数；scale=1，min/max_offsets 为空，values 每点四字节。pool_rows/columns 等于 original_rows/columns。仅允许 4:1；保持预览 protocol_version=1 和既有枚举数值。

文件：infra/proto/preview.fbs、sidecar/preview、frontend/internal/preview、frontend/ui/src；生成代码随协议更新。沿用现有 C++、Go、TypeScript 风格和测试框架。

验证命令（仓库根）：
- `cd workspace/infra/frontend/ui && npm ci && npm test && npm run build`
- 原生 ARM：`docker build -f workspace/infra/sidecar/Dockerfile --target runtime .`
- 原生 ARM：`docker build -f workspace/infra/frontend/Dockerfile .`

验收：1/8 通道完整点序列、int16 极值和尾点逐字节/逐点一致；非法长度、类型、维度、偏移拒绝；旧 IQ/PC/RD 测试通过；真实服务器两节点浏览器收到 8192 点、帧号前进且绘图非空。

边界：不改 Source、SDK 主数据格式、SignalSink、采样率或录制格式；不做插值假充原始点；不启动录制，不删除用户数据。保留预览有界内存和丢展示帧机制。仅在录制空闲或部署已停止时更新，发布固定 digest 并保留回退信息。
