# RawIQFrame 实施与验证结果

本页保留开发态验证证据。后续 **新镜像发布及固定 digest 仿真已完成；真实 PCIe 10 秒首检无 IQ/控制数据，硬件录制仍未通过**，详见 [发布与首检](release-results.md)。以下离线证据不能替代硬件关卡。

## 实现

- SDK 新增 `RawIQFrame 4:1`：原始 TX/RX、动态通道/样本维度、小端 CS16；旧 IQ 1:3、PC 2:2、RD 3:2 JSON 未修改。
- `Output<T>::try_create`、可选端口打开超时；旧默认构造/create/write 保持兼容，Ring 布局/ABI 不变。
- 新增独立仿真 Worker `signalsource_raw_iq`，静态输出标签 4:1；旧 signalsource 默认入口不变。
- PCIe 路径实现控制表配对、每通道 8192 点硬件帧、单路输出、有界队列、故障及退出；不加载 CPI 模板，不做硬件 TX。
- SignalSink src/、保存格式及写入逻辑**没有修改**；测试仅重编译现有实现并绑定输入 4:1。

## 检查结果

| 检查 | 结果 | 边界 |
| --- | --- | --- |
| 本地 SDK CTest | 4/4 | 包括非对称 2×3 黄金字节、非法尺寸、端口超时、非阻塞输出与旧契约 |
| 本地 PCIe CTest | 15/15 | 接收/组帧、单路选路、队列满、关闭、排空超时、SIGTERM；无真实设备 |
| 原生 ARM SDK / 仿真 Source / PCIe / 原有 SignalSink | 4/4、3/3、15/15、4/4 | 临时隔离构建和离线测试，不是镜像发布 |
| ASan+UBSan SDK / PCIe | 4/4、14/14 | 本地；未把未运行的 TSan 算通过 |
| release Worker 布局 / SignalSink 契约脚本 | 通过 | 静态/脚本检查，不是 Harbor push/pull |
| 本地真实 Sidecar 仿真闭环 | 64 帧逐字节通过 | amd64 Worker + QEMU ARM Sidecar，不计 native 性能 |
| `.64` 原生仿真闭环 | 64 帧逐字节通过 | native Source + native Sidecar + native Sink，显式 TCP、开发二进制挂载 |
| 新 PCIe 硬件接收及录制 | **首检未通过** | 发布后 10 秒只收到 BIT；见后续报告，录制未开始 |

原生 SDK/PCIe 最后一次复测分别见 `evidence/raw-iq/native-sdk-final.txt`、`native-pcie-final.txt`；首轮全部构建日志为 `native-tests.txt`。本地 sanitizer 日志与最终测试记录同目录。

## 服务器实际文件

主机 `192.162.2.64`，aarch64，Docker 19.03.15。使用两个真实发布 Sidecar（`d400a3b5…` digest），Source/Sink 是原生临时构建的测试二进制，挂载在既有 ARM 构建镜像中。镜像身份、挂载、容器 ID、计数和破坏检查见 [native-pipeline.json](evidence/raw-iq/native-pipeline.json)。**不是已发布 Worker 镜像验收。**

文件位于真实磁盘 `/dev/sda4`、ext4：

```text
/root/workspace/captures/raw-iq-native-proof-4/smoke/2db22851695adc11d61a183980d06b47.sink
```

- 64 帧 × 单通道 × 8192 点 = 524288 个 CS16 样本。
- 完整裸帧合计 2102784 字节；Source 先于录制停止并排空，Sink accepted/written 均为 64，队列结束为 0，正常 footer/sync 完成。
- 全部 IQ 及两个时间戳按确定性源逐字节验证；首个 TX 全 F、RX 高位及超过 2^53 的整数保留。
- IQ 单字节修改、TX 单字节修改和文件截断均被拒绝。
- 业务诊断指纹 FNV-1a64：`1645031447842536997`（不是密码学证明）。
- 裸帧串 SHA-256：`014f13aaa02193e0fcacfc4570497797ad9aa01f3472d04f122ce3db5e7e06ba`。
- 整个 USINK001 文件 SHA-256：`20e8ccd89cb9ea17c80b16086b9328a3df8b1a87bfb426da9f7820d301de5ea2`；服务器与取回文件一致。

上述开发态仿真没有启动 PCIe 采集、没有映射 `/dev/mem`，没有变更固件、配置、VM、调度、TRIM 或防火墙。没有修改/重启原 `uestcradar-web`；测试结束只清理本轮测试容器，服务器仍仅原 Web 运行。约 2 MiB 的本轮正常测试文件保留。

### 验证中发现并处理的问题

1. 旧 SDK 输出构造在端口不存在或头部未初始化时无限等待。新增有界重载并用实际超时回归验证；旧默认语义保留。
2. 旧本地 SignalSink 测试镜像缺少 process_id，测试在发出 start 前拒绝；改为重编译当前未修改的 SignalSink。
3. 原生服务器没有 Compose 插件、Docker API 为 1.40。使用客户端 Compose 经 SSH 管理隔离测试；确认镜像/主机均 ARM 后，仅测试 override 移除不支持的 platform 参数，没有升级服务器 Docker。
4. 初次原生文件位于 `/tmp`（tmpfs），未计为磁盘证据。改为上述 ext4 目录重新验证，并让测试脚本拒绝 tmpfs/ramfs。

## 尚未通过的关卡

- 原先本地 `4dc944c` 与远端 `d2c8a5f` 不一致的发布阻塞已在用户授权后解除；代码提交为 `fdb78cc`，镜像及实际 digest 见后续报告。
- 当前阻塞变为真实接收没有 IQ/控制包。数据恢复后仍须完成描述字配对/选路检查、10 秒冒烟、60 秒完整录制、真实断链/背压测试。当前单元测试不证明 DMA 所有权或 ADC 绝无漏采。
- 本次低速小文件仿真不验证长期磁盘带宽。历史约 123 MB/s 单通道输入仍可能高于当前盘约 101 MB/s 的长写能力；不降采样、不丢帧、不用写缓存或八通道假设掩盖。
- Web strict-RDMA、算法处理、新帧绘图均未纳入本次仿真通过结论。
