# 板卡配置与 RawIQFrame 部署

[总览](../README.md) · [底层接收](../pcie/README.md) · [当前结果](../tests/raw-iq-results.md)

## 硬件配置来源

配置来自既有 `cycore/lib/du/resource/device/pcie/pcie_config/`。保留 `SYNC_1_4 | DRP_4G8` 对应的 `config_sync_4.txt`、`config_drp_4g8.txt` 和 `log.json`，安装至 `/app/pcie_config/`。选中八路中的一路不意味着修改光纤配置或设备启动顺序。

新路径没有雷达参数模板：不读取 `CPI0..CPI9`、`input.bin`，不使用 `CPI_DATA_IMAGE`、`--data-root` 或假 PRT。帧为 `RawIQFrame 4:1`，通道数 1、每通道 8192 点及两个原始时间戳。

## Docker 部署约定

- 使用已验证的新 SDK `ALGO_BASE` 不可变引用；旧基座缺少 RawIQFrame/非阻塞输出时编译应失败。
- 入口 `/app/pcie_source`，工作目录 `/app`。
- Worker v2 标签为 `roles=source`、`input=none`、`output=4:1`。
- Source Sidecar 的 Upstream 为 disabled，Downstream 连接接收端；两端具体契约都为 4:1。
- SDK Payload 容量至少 32792 字节（24 字节 Metadata + 32768 字节 IQ），不是只按样本区分配。
- Worker 与对应 Source Sidecar 共享 IPC；`UESTCRADAR_DOWNSTREAM_SHM_NAME` 必须指向该 Sidecar 输出 Ring。
- 接收端 SignalSink 配置 `SIGNALSINK_INPUT=4:1` 与持久目录。录制实现、USINK001 格式和 RawFrame 输入不修改。

## Harbor 发布与目标机部署

正式流程：原生 ARM 构建验证 → SDK/Worker 发布 Harbor → 按实际 digest 拉取 → 硬件运行。遵循仓库 `.agents/skills/docker-release/`：远端已有仓库必须干净且 HEAD 与本地一致，SDK 先于依赖 Worker 发布，不覆盖不可变 Tag。

临时单元测试、挂载二进制、QEMU 或本地 image ID 不替代正式发布；不使用 save/load 绕过 Harbor，不自动调整凭据、证书或网络。发布操作需要记录源码版本、SDK digest、Worker digest、测试及部署证据。

完成候选镜像发布后，独立硬件检查示例：

```bash
: "${IMAGE_REF:?set published pcie-source digest}"
docker pull "$IMAGE_REF"
mkdir -p "$PWD/pcie-logs"
# 先完成下面的设备核对；这条命令会初始化硬件。
docker run --rm --name pcie-source-capture \
  --device /dev/mem:/dev/mem --cap-add SYS_RAWIO \
  --mount "type=bind,src=$PWD/pcie-logs,dst=/logs" \
  "$IMAGE_REF" --capture-only --channel 0 --duration-seconds 10 \
  --timestamp-errors /logs/rx_timestamp_errors.jsonl
```

正式输出时去掉 `--capture-only`，另外连接已核对的 Source Sidecar IPC/SHM；先开启 SignalSink 录制，再启动有限时长 Source。结束采集后核对提交量、排空在途帧，再停止并同步录制。不能复用旧 1:3 端口或直接连旧算法。

Web 编排使用 strict-RDMA；[独立 TCP 仿真夹具](../tests/compose.raw-iq.yaml)只是显式诊断，不是自动降级方案。

## 实施前需确认

| 项目 | 核对内容 |
| --- | --- |
| 设备 | 板卡/固件、BAR/预留内存、现有进程占用、`/dev/mem` 权限 |
| 协议 | 当前控制表与两个 IQ 段的 PCIe 描述字顺序及长度 |
| 软件 | 已发布镜像的 SDK/类型匹配、单通道选择、SHM 名称及容量 |
| 存储 | 真正的磁盘目录而非 tmpfs；实际输入速率、容量与同步时间 |
| 退出 | 有界队列和排空超时；失败不冒充连续记录 |

> [!WARNING]
> 硬件初始化会写既有寄存器。不得在未知板卡上运行，不默认使用 `--privileged`，不同时启动两个接收者，不改变地址、固件或配置来绕过失败。

当前没有新 RawIQFrame PCIe 路径的发布/硬件通过记录；旧版独立采集数据仅作历史参考。
