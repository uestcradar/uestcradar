# 板卡配置、参数模板与 Docker 部署

[返回总览](../README.md) · [底层接收](../pcie/README.md) · [Worker 输出](../src/README.md)

> [!NOTE]
> 参数加载、配置副本和 Dockerfile 已实现；此前 ARM64 临时镜像已做独立采集。最新优化版本经用户授权例外在目标机临时构建并上板复测：channel 0 约 30.693 MS/s，RX 预期差值按用户要求改为 98304，30 秒无时间戳异常；尚未正式发布，SDK 输出也未实现。以下为规范部署流程。见 [实际记录](../tests/hardware-results.md)。

## 硬件配置来源

来源：`/home/zikun/code/common/cycore/lib/du/resource/device/pcie/pcie_config/`。

来源包含 `config_sync_4.txt`、`config_sync_6.txt`、`config_sync_8.txt`、`config_timer.txt`、`config_drp_4g8.txt` 和 `log.json`，不是所有旧枚举配置都有对应文件。

以相邻适配器使用的 `SYNC_1_4 | DRP_4G8` 为迁移基线，对应四根光纤承载八路 IQ。迁移必要配置和 BIT 日志文件，记录适用板卡/固件，不因逻辑 IQ 有八路就改用 `SYNC_1_8`。

旧代码通过 `./pcie_config/...` 读取文件；容器工作目录拟沿用 `/app`，硬件配置放在 `/app/pcie_config/`。实现需明确配置路径，不依赖开发机绝对路径。

## 联调默认参数

**唯一默认参数来源是同版 signalsource 的 CPI 数据集，不维护另一套假参数表。** 保持 [IQFrame 契约](../../../infra/sdk/README.md) 不变；硬件不提供的边界和参数由该模板代替，仅用于联调。

| 内容 | 确定规则 |
| --- | --- |
| 选中逻辑通道 | 默认 `0`，范围 `0..7` |
| `channel_count` | `1` |
| `samples_per_channel` | 固定 `751206` 个复数点 |
| `pulse_count` | 固定 `64` |
| 样本区大小 | `3004824` 字节，小端 CS16 |
| `cpi_index` | 从 0 开始的软件序号，不使用循环模板的原始序号作为输出序号 |
| 参数模板 | 第 k 个软件帧使用 `CPI[k % 10]`，循环 `CPI0..CPI9` |
| 其余标量及逐脉冲数组 | 按 signalsource 的加载规则原样读取，不推算或覆盖为另一套默认值 |

以 [signalsource/src/cpi_data.hpp](../../signalsource/src/cpi_data.hpp) 为参数加载和校验基准：

| 模板文件 | 用途 |
| --- | --- |
| `metadata.json` | 尺寸、波形类型、过采样率、采样率、载频、带宽、脉宽、PRT、观测距离、反量化缩放等 |
| `pulse_time.txt` | 64 个 `pulse_time_offset_s` |
| `pulse_phase.txt` | 64 个 `pulse_phase_rad` |
| `pulse_freq.txt` | 64 个 `pulse_frequency_hz` |
| `wd0.txt` | 64 个 `coherent_weight` |
| `input.bin` | **不读取、不要求存在**；由选中通道的实时 PCIe 样本替代 |

沿用 `--data-root` 语义，默认 `/data`，其下包含 `CPI0` 至 `CPI9` 的上述参数文件。十份模板在启动硬件前一次性加载和校验：格式版本、CS16 布局、每帧尺寸、原始模板序号和四组数组长度均需匹配当前 signalsource；各 CPI 的公共波形配置应一致，逐脉冲数组允许随模板变化。缺文件、非法数值或不兼容形状直接报错，不退回自造参数。

首版不支持单独覆盖采样点数、脉冲数或物理参数；需要更换模板时整体替换兼容的数据根目录。将来若改变公共输出形状，应同步调整 signalsource、下游及容量配置。

> [!IMPORTANT]
> 这些参数对离线数据有其原始含义，对实时 PCIe 数据只是联调模板。不用于编程硬件采样率、时序、射频或 DRP，不按假时序补样、重采样或延时发帧。攒满实际样本即提交输出，受现有背压控制。启动日志打印 `synthetic_metadata=true`、模板来源、通道及生效参数，不新增帧字段。

## Docker 部署约定

`pcie_source/` 根目录已有 `Dockerfile`，参考 [signalsource/Dockerfile](../../signalsource/Dockerfile)，目标是容器化数据源可替换：

- 使用相同 `ALGO_BASE` 构建参数及 Worker SDK；工作目录 `/app`，入口为 `/app/pcie_source`。
- 使用相同 `CPI_DATA_IMAGE` 参数及与 signalsource 对齐的固定数据镜像版本。构建时提取 `CPI0..CPI9` 的五类参数文件到 `/data/`，无需把 `input.bin` 放入最终运行镜像。
- 镜像内包含编译后的 Worker、必要底层接收依赖及硬件配置，不依赖宿主机 Cycore 源码目录。
- 镜像标签沿用 `io.uestcradar.contract=worker/v2`、`roles=source`、`input=none`、`output=1:3` 对应的完整标签键，与 signalsource 一致。
- 沿用 `UESTCRADAR_DOWNSTREAM_SHM_NAME` 及配套 Sidecar 输出配置。Source Upstream 为 `disabled`；下游 Worker 的通信接口、IQ 类型及形状保持不变。
- 相比 signalsource，部署额外提供板卡设备权限、硬件配置和 `--channel` 选路；不改变下游数据接口。

单帧 IQ v3 Metadata 加样本共 `3006960` 字节，SHM/Ring 还需按 SDK 规则预留帧开销。复用现有 signalsource 的容量配置并校验，不能只按样本区大小分配。

Dockerfile、构建入口和独立采集命令现已创建，实际执行记录见硬件验收文档；正式输出不可用。参数一致不意味着离线测试结果一致：原有针对 `input.bin` 的哈希及精确结果校验必须与实时采集验收分开。

## Harbor 发布与目标机部署

**正式流程固定为：构建 ARM64 镜像 → 验证 → push Harbor → 目标机按 digest pull → Docker 运行。** 该要求同样适用于后续硬件验收，不以目标机本地构建代替发布链路。

- 镜像仓库：`registry.chengyistudio.com/cxx/worker`，版本标签使用 `pcie-source-<唯一版本>`，已发布版本不得覆盖。
- 在具备 ARM64 原生构建能力或已验证交叉构建能力的构建机/CI 上构建；采集目标机 `192.162.2.64` 仅拉取和运行，不接收源码、不现场编译。
- `.64` 不使用 `docker save/load`、拷贝可执行文件或挂载开发源码作为 Harbor 发布替代。历史临时验证保留记录，不能计为此流程已通过。
- DNS、网络、认证、证书或构建架构问题均记为阻塞，先报告；不得自动修改目标机网络/证书策略或绕过镜像仓库。
- 发布记录包含源码版本/工作区状态、构建参数、基础与数据镜像版本、测试结果及 Harbor 实际返回的 manifest digest。镜像本地 image ID 不等于仓库 manifest digest。

以下命令从 `pcie_source/` 执行，要求构建机已具备 ARM64 构建/检查能力和仓库访问权限；凭据不写入命令、文档或源码：

```bash
: "${VERSION:?请设置唯一发布版本}"
IMAGE="registry.chengyistudio.com/cxx/worker:pcie-source-${VERSION}"
docker build --platform linux/arm64 --pull -t "$IMAGE" .
bash tests/check_image.sh "$IMAGE"
docker push "$IMAGE"
docker image inspect "$IMAGE" \
  --format '{{range .RepoDigests}}{{println .}}{{end}}'
```

在 `.64` 上，将实际发布记录中的 `registry.chengyistudio.com/cxx/worker@sha256:...` 完整引用设为 `IMAGE_REF`，不填写虚构摘要，不用可变标签代替：

```bash
: "${IMAGE_REF:?请设置 Harbor 发布记录中的完整 digest 引用}"
docker pull "$IMAGE_REF"
docker image inspect "$IMAGE_REF" \
  --format 'id={{.Id}} arch={{.Architecture}} digests={{json .RepoDigests}}'
# 核对板卡地址、设备占用和权限后执行；此命令会初始化硬件。
mkdir -p "$PWD/pcie-logs"
docker run --rm --name pcie-source-capture \
  --device /dev/mem:/dev/mem --cap-add SYS_RAWIO \
  --mount "type=bind,src=$PWD/pcie-logs,dst=/logs" \
  "$IMAGE_REF" --capture-only --channel 0 --duration-seconds 30 \
  --timestamp-errors /logs/rx_timestamp_errors.jsonl
```

RX 时间戳异常日志追加写入宿主机 `pcie-logs/rx_timestamp_errors.jsonl`，不随 `--rm` 删除；按 `run_id` 区分每次运行。请预留异常日志空间；无法打开、写入或刷新时进程报错，不能悄悄跳过记录。

当前**尚未完成本项目镜像的 Harbor push/pull 验收**。此前 `.64` 本地构建镜像及其采集日志仅是临时功能证据，后续需使用仓库拉取镜像重新验收。

## 实施前需确认

| 项目 | 所需信息 |
| --- | --- |
| 硬件身份 | 板卡、FPGA 固件及目标主机平台 |
| 接收部署 | 物理地址、预留内存、BAR、数据端序及既有八路映射的板上验证 |
| 运行资源 | `/dev/mem` 权限、CPU/绑核策略及共享内存容量 |
| 实时性 | 有界队列容量、允许延迟；默认队列满丢新完成帧 |
| 数据模板版本 | 与部署的 signalsource 使用相同数据镜像及参数文件 |

完整硬件 CPI 协议不再是首版链路联调的前置条件。

## 运行安全

旧库使用 Linux `/dev/mem`，不是通用 `/dev/xdma*` 接口，物理地址和映射大小具有平台约束。

> [!WARNING]
> 硬件配置加载会写寄存器。必须先确认板卡、地址及预留内存，不能在未知主机直接执行。同一板卡接收路径只允许一个进程拥有。联调参数模板不能替代这些检查。

容器按宿主机策略授予必要设备权限，不默认使用全特权容器。参数模板与板卡寄存器配置分开管理，前者不能被误写入硬件。
