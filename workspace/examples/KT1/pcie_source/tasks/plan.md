# pcie_source 实施计划

[总览](../README.md) · [任务清单](todo.md)

状态：独立采集路径已实现，正式 SDK 输出仍待实施；新增双时间戳协议适配与诊断已通过离线检查，后续按用户要求将预期间隔改为 98304，并优化双快照读取；目标机 channel 0 的 30 秒检查已达到约 30.693 MS/s，112400 次比较无异常。正式 Harbor 发布仍待执行。现有分目录 README 是需求来源，本计划不另建重复 SPEC。

## 固定范围

- 4 根光纤 → 8 路 IQ，沿用已有映射；选择 `0..7` 中一路。
- 每帧 751206 个连续有效复数样本，64 脉冲；循环使用 signalsource 的 `CPI0..CPI9` 参数模板，样本来自 PCIe。
- 继续使用 `IQFrame 1:3`，不修改 SDK 契约，不读取离线 `input.bin`。
- 满缓冲只丢完整软件 CPI，不改切分边界；真实采集缺口使部分块失效。
- 根目录 Dockerfile 与 signalsource 对齐；镜像必须先在构建机完成 ARM64 构建验证并 push Harbor，再由 `192.162.2.64` 按 digest pull。目标机不接收源码或现场构建；先独立采集，再接 SDK/Sidecar。

## 实现约定

以下独立采集接口已实现：

- `--channel 0..7`，默认 `0`。
- `--data-root PATH`，默认 `/data`；`--pcie-config-dir PATH`，默认 `/app/pcie_config`。
- `--capture-only`：接收、解析和分块后本地释放，不初始化 SDK 输出、不等待 SHM、不启动 Sidecar。
- `--duration-seconds N`：使用单调时钟限制运行时间，默认 `0` 表示持续运行；检查模式 30 秒是首次观测窗口，不是已证明足够的硬件性能指标。
- `--timestamp-errors PATH`：默认追加到 `rx_timestamp_errors.jsonl`。按 [新协议](../pcie/control-protocol.md) 默认校验相邻 RX 差值为 98304，记录全部异常；TX 全 F 不报错，时间戳异常不清空 CPI。容器挂载日志目录保证持久保存。
- 独立检查期间无有效 IQ，或无法完成一个完整软件 CPI，必须报告未通过，不能仅因正常退出判成功。时间戳连续性另看 `[timestamp-check]`，不以数据存在性退出码代替。

不设置任意软件分块尺寸，不开放单字段雷达参数覆盖，不默认全特权容器，不进行硬件 TX。

## 接收吞吐优化（本轮）

目标：在 `.64` 上尽量持续消费 IQ；以标称 30.72 MS/s/通道的 99%（30.4128 MS/s）作为吞吐对照检查线，不以平均速率替代数据完整性证明。用户最新指定 RX 时间戳预期间隔为 **98304**，不再是 8192。

先基线复测和分段计时，再按占比优化 DMA 读取和每包分配。保留两次独立读取比对、DMA 边界校验、完整异常保存及软件 CPI 切分；不靠关闭校验、丢弃日志或只数描述字提高吞吐。不更改 MMIO 内存属性、硬件配置或 SDK 契约。只有实测仍有瓶颈时才增加线程/队列。

每步运行 CTest，硬件先 10 秒对照、最终 30 秒复测；记录镜像身份、选中通道 MS/s、接收计数、时间戳差值分布、无效包/变化副本，以及日志条数与统计是否一致。沿用本次用户明确允许的临时目标机 Docker 测试方式，不抵扣 T08H。可用 `tests/check_capture.py CAPTURE_LOG ERRORS_JSONL --min-msps 30.4128` 重放检查，历史阈值从日志读取。

## 组件与依赖

| 组件 | 职责 | 依赖 |
| --- | --- | --- |
| `pcie/` | 配置加载、映射、接收描述字、覆盖检查及停止 | Linux、板卡协议、必要旧 C 代码 |
| `src/` 解析/组帧 | 8 路映射、选路、连续性、固定分块、模板加载 | 标准库、SDK 类型；解析测试不访问硬件 |
| Worker 主流程 | 采集检查模式、正式有界队列与 SDK 输出 | 上述两层 |
| Docker | 构建 Worker，打包硬件配置及模板 | 同版 ALGO_BASE、CPI_DATA_IMAGE |
| 验证 | 纯数据检查 → 独立硬件采集 → SDK/Sidecar → 背压 | 对应实现阶段 |

构建顺序：纯数据路径 → 底层接收收敛 → 独立采集 Worker → Docker 构建 → Harbor push/pull 验证 → 独立硬件检查 → SDK 输出/背压 → 端到端验收。

纯数据路径与底层代码审查可独立开展，默认顺序执行，不启动委派或并行写入。硬件验收失败时停在采集阶段，不用下游链路掩盖底层问题。

## 代码与构建约定

C++20，复用 `cycomm::sdk`；旧底层代码按 C 构建，使用 Threads。CMake/CTest 沿用现有示例，不新增测试框架。明确所有权、边界及错误返回；函数/变量使用项目已有的 snake_case，类型使用 PascalCase。

风格参考现有 signalsource：

```cpp
uestcradar::Output<uestcradar::IQFrame> output;
auto frame = output.create(metadata);
// 校验形状并填满全部样本后才提交。
output.write(std::move(frame));
```

采集线程不调用阻塞的 SDK 输出；完整帧队列有界；缓冲所有权不能在采集、队列及在途帧之间重叠。

## 目标命令

以下离线构建命令在构建机的 `pcie_source/` 执行，需已安装 SDK。构建与独立采集代码已存在；正式 SDK 输出仍未实现。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

镜像发布必须执行 [Harbor 发布流程](../pcie_config/README.md#harbor-发布与目标机部署)，记录不可变标签和实际 manifest digest。硬件测试在 `192.162.2.64` 上使用仓库拉取镜像执行，`IMAGE_REF` 来自发布记录。核对板卡地址、预留内存、设备占用与 `/dev/mem` 策略后运行：

```bash
: "${IMAGE_REF:?请设置 Harbor 发布记录中的完整 digest 引用}"
docker pull "$IMAGE_REF"
mkdir -p "$PWD/pcie-logs"
docker run --rm --name pcie-source-capture \
  --device /dev/mem:/dev/mem --cap-add SYS_RAWIO \
  --mount "type=bind,src=$PWD/pcie-logs,dst=/logs" \
  "$IMAGE_REF" --capture-only --channel 0 --duration-seconds 30 \
  --timestamp-errors /logs/rx_timestamp_errors.jsonl
```

该候选命令不保证适用于目标内核；权限受限应报告具体原因，先确认必要权限，不能自动改成 `--privileged`。实际运行命令、镜像摘要及日志记入验收记录。不得在构建、CTest 或镜像启动自检中隐式访问硬件。

## 风险与检查点

| 风险 | 控制与阶段关卡 |
| --- | --- |
| 旧指针在复制期间被 DMA 覆盖 | 审查并实现可用的一致性检测；无法证明有效的数据不提交，不以长度满 N 替代连续性证据 |
| 旧覆盖计数/队列并发及异常清理不可靠 | 接收迁移阶段检查所有实际调用路径，显式处理初始化失败、停机和残留队列 |
| SDK 输出等待不能取消 | 正式输出阶段核对现有 API；若无法安全退出先报告，不私自改 SDK 契约或引入不安全线程取消 |
| 模板加载器强制读取 input.bin | 复用解析规则，做最小参数加载拆分；不加载再丢弃整个离线样本集，不改 signalsource 既有行为 |
| 下游慢导致采集停顿 | 独立输出线程、有界完整帧队列；用真实阻塞及恢复检查帧内容 |
| 物理地址/固件不匹配 | 首次访问设备前核对；仅用目标主机 Docker 验证 |
| 假 Metadata 被当成测量值 | 配置、日志及验收记录明确 synthetic_metadata=true；不验证真实距离/速度 |

## 边界

- **始终**：校验信任边界、保留必要迁移版权信息、保证内存有界；每项实现留可运行检查；分开记录完整帧丢弃和采集缺口；变更决定先同步文档。
- **先确认**：硬件地址/固件变更、额外设备写操作、新依赖、SDK 行为或契约变更、扩大容器权限、修改其他示例共享代码。
- **禁止**：以拷贝源码/二进制、目标机现场构建或 save/load 绕过 Harbor 发布；仓库不可达时隐瞒阻塞；伪造硬件通过记录、用离线数据冒充采集、默认补零、跨缺口拼接、提交半帧、静默覆盖在途缓冲、提交凭据。

## 完成标准

1. 无硬件测试通过，完整模板无需 `input.bin` 即可加载。
2. `192.162.2.64` 上 Docker 独立采集检查八路有数据、软件组帧完整、停止可控；提供实际证据。
3. 正式输出与 signalsource 的契约、尺寸及模板一致，下游无需改通信接口。
4. 堵塞/恢复期间只丢完整 CPI，实际收到帧逐样本完整；内存有界，退出不悬挂。

目标主机架构、设备权限、DMA 一致性机制及 SDK 等待取消能力是实施时必须核实的事实；本计划不把它们视为已验证。
