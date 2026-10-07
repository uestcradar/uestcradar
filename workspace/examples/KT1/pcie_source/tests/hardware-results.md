# 实施与硬件验收记录

[TODO](../tasks/todo.md) · [验证要求](README.md)

> [!IMPORTANT]
> 本文已有硬件结果来自此前“传源码到 `.64` 本地构建、save/load 导入依赖镜像”的临时开发验证，**不是 Harbor 标准发布验收**。按用户要求，后续统一执行构建机 push Harbor、目标机按 manifest digest pull；不再以现场构建或 save/load 绕过仓库。T08H 发布验收尚未执行，原日志原样保留，不追认成符合新流程的结果。

此前按用户要求暂跳过 T08H，进行了旧协议逐控制包序号诊断：[frame-sequence-results.md](frame-sequence-results.md)。30 秒观察 71063 个控制表，24272 次向前跳号，0 次重复/回退；只描述当时的序号观察，不追认 Harbor 发布通过，也不代表新版时间戳校验。

## 最新结果：98304 阈值与读取优化

按用户最新要求改为 RX 差值 98304，并将大包双快照复制分为两半，由主线程和一个协作线程并行处理。计时、完整对照与最终记录见 [throughput-results.md](throughput-results.md)。最终 `.64` 上 channel 0 采集 30 秒达到 **30.692891 MS/s**，112400 次 RX 比较全部相邻，0 个无效包、0 次副本变化；仍标记 `integrity_verified=false`，不冒充逐样本或八路完整性验收。

本地 CTest/UBSan 各 12/12，ARM64 CTest 11/11；协作复制单测通过 ASan+UBSan，TSan 环境启动失败已如实记录。镜像 `pcie-source:rx98304-parallel`，宿主记录目录 `/root/pcie-optimized-30s-NJ0Sqt`。本轮仍属用户允许的临时构建测试，Harbor 发布未完成。

## 双时间戳首次测试（当时阈值 8192）

用户随后明确授权本次临时在 `.64` 构建并测试，详细结果见 [RX 时间戳实测](rx-timestamp-results.md)。ARM64 CTest 10/10；30 秒收到 141017 个 IQ 包，完成 768 个软件 CPI。71889 次时间戳比较全部异常，实际差值均为 98304 的整数倍，校验阈值仍保持 8192。全部异常已在宿主机保存并归档；T08H 不因此算通过。

以下为此次硬件测试之前的离线检查和当时的部署阻塞记录：

按用户确认的新协议，已移除旧序号解析/检查，改为默认检查相邻 RX 时间戳的 uint64 模差为 8192，TX 全 F 不报错，全部异常追加 JSONL。时间戳异常不清空部分 CPI；同轮修正了控制包/BIT 被当成无数据而休眠的问题。此次没有改变板卡寄存器配置或 SDK 契约。

- 本地 CTest **11/11**：[原始结果](evidence/ctest-rx-timestamp.txt)。
- UBSan CTest **11/11**，使用 `-fsanitize=undefined -fno-sanitize-recover=all`，含模板差分：[原始结果](evidence/ubsan-rx-timestamp.txt)。
- 新测试覆盖两个 64 位字段、TX 全 F、非对齐和短载荷、首帧基线、8192 正常间隔、重复、前跳、回退、64 位回绕、100 条异常 JSONL，以及真实 `PcieReceiver::poll()` 在异常后不丢弃部分 CPI。
- 将日志路径设为 `/dev/full` 的失败检查退出 `1`，报 `basic_ios::clear: iostream error`，在加载 PCIe 配置/打开设备之前失败，没有触碰硬件：

```bash
/tmp/pcie-source-build/pcie_source --capture-only \
  --data-root /tmp/pcie-source-templates \
  --pcie-config-dir /__pcie_no_hardware__/config --timestamp-errors /dev/full
```

**在上述离线检查阶段，尚未执行新镜像构建、Harbor 发布或 `.64` 采集；后续经用户明确授权的例外测试见本节开头链接。** T08H 仍按要求跳过；本机 `docker buildx ls` 的 ARM64 builders 指向不可连接的 `/home/zikun/.docker/desktop/docker.sock`，默认 builder 仅列出 x86 平台。本次没有把源码/二进制传到目标机，也没有现场构建或 save/load 绕过发布。待可用 ARM64 构建机和发布流程恢复后，以新镜像完成 30 秒复测并核对持久异常记录。

## 旧协议历史结论

**旧版 channel 0 曾通过“有数据并持续完成定长软件分块”的检查：30 秒收到 134460 个 IQ 包，组成 733 个软件 CPI，退出码 0。** 当时按用户要求只提取序号，不校验其连续性。此前两次失败结果保留在下文，不能与当前双时间戳实现混为一谈。八路逐路正确性、DMA 连续性和下游验收仍未完成，T09 不整体勾选通过。当前二进制仅支持 `--capture-only`，正式 SDK 输出明确报错，不伪装成可替换 signalsource 的完整实现。

另外，迁移接口没有可证明 DMA 复制期间一致性的硬件租约/ACK。两次样本复制比对只用于发现变化，不能证明未丢样；因此始终报告 `integrity_verified=false`。SDK 公开 `Output` API 也未提供等待取消方法，这些仍是正式输出的阻塞项。

## 已执行的本地验证

宿主 x86_64，使用仓库当前 SDK 编译安装到临时目录；未修改 SDK 或 signalsource 源码。

```bash
cmake -S uestcradar/workspace/sdk -B /tmp/pcie-source-sdk-build \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX=/tmp/pcie-source-sdk-install
cmake --build /tmp/pcie-source-sdk-build --parallel 4
cmake --install /tmp/pcie-source-sdk-build

cmake -S uestcradar/workspace/examples/pcie_source -B /tmp/pcie-source-build \
  -DCMAKE_PREFIX_PATH=/tmp/pcie-source-sdk-install -DBUILD_TESTING=ON \
  -DCPI_REFERENCE_DATA_ROOT=/tmp/pcie-source-templates
cmake --build /tmp/pcie-source-build --parallel 4
ctest --test-dir /tmp/pcie-source-build --output-on-failure
```

结果：9/9 通过。模板从下列固定镜像提取，差分检查直接调用既有 `signalsource/src/cpi_data.hpp` 加载器，对十帧所有 Metadata 字段及逐脉冲数组逐项比对。检查目录含离线数据仅为参考加载器服务，pcie_source 本身不读取 input.bin。

另在 `/tmp/pcie-source-ubsan` 使用 C/C++ 编译选项 `-fsanitize=undefined -fno-sanitize-recover=all` 重建，8/8 非差分检查通过。

## 主机及部署事实

- 主机：`root@192.162.2.64`，`node4-1`，aarch64。
- PCIe：`04:00.0`，Xilinx `10ee:7038`，BAR 区间 `ef000000-ef0fffff`。
- `/proc/iomem`：`2080000000-20ffffffff` 标为 `memmap reserved`；覆盖迁移接收映射范围。
- `/dev/mem` 存在；测试前 `fuser /dev/mem` 未发现占用。未修改其他容器或系统网络/时钟。
- 沿用 `SYNC_1_4 | DRP_4G8` 配置，未验证 FPGA 固件具体版本；只执行既定接收初始化，没有额外 TX 或复位操作。
- 控制机记录日期为 2026-09-29；目标机时钟停留在 2021-01-01，原始构建日志出现 clock-skew 警告。采集时长使用单调时钟，不受墙钟日期影响。未擅自更改目标机时钟。

## 镜像构建

本地 x86_64 构建 ARM64 镜像失败：`exec /bin/sh: exec format error`。转到目标 ARM64 主机原生构建，未安装全局 QEMU 或更改主机配置。

目标机直接拉取固定数据镜像遇到 DNS 超时，因此使用控制机已缓存的同一镜像经 `docker save/load` 导入，构建时覆盖数据镜像引用为导入标签。校验 image ID 相同，而非随意替换数据版本：

- 数据镜像 digest：`sha256:c6833658a15367f4c4b115fa6391f563af26c7a04853519379f424da72c5feb9`。
- 本地/远端数据 image ID：`sha256:88f4d61bfa31f7d87947ababb043ab17ae4432ef684a85511e17e34145d406f0`。
- 基础镜像 image ID：`sha256:7882db05de5a015a37c0596b573a10075ec677990d16bbdf7e7c8fdc1e44fcae`。
- 首次测试镜像 `pcie-source:dev`：`sha256:a081a43fc944f37f8384ff32e666ecf8bf8b58590074876c33f30ea5085b1542`。

在目标机 `/tmp/pcie-source-build-context` 执行：

```bash
docker build \
  --build-arg CPI_DATA_IMAGE=registry.chengyistudio.com/cxx/worker:signalsource-cpi0-cpi9-20260802 \
  -t pcie-source:dev .
bash tests/check_image.sh pcie-source:dev
```

镜像构建阶段 CTest 8/8 通过，权威模板加载成功；镜像检查确认五类模板文件齐全、不包含 input.bin、入口及 output=1:3 标签正确。原始构建记录见 [docker-build-initial.txt](evidence/docker-build-initial.txt)。

## 首次独立采集命令与结果

无 SDK/SHM/Sidecar/下游 Worker，最小设备权限候选命令成功打开映射：

```bash
docker run --rm --name pcie-source-capture \
  --device /dev/mem:/dev/mem --cap-add SYS_RAWIO \
  pcie-source:dev --capture-only --channel 0 --duration-seconds 30
```

[完整原始日志](evidence/capture-channel0.txt)；退出码 `2`。

| 指标 | 结果 |
| --- | --- |
| elapsed_s | 30.000005237 |
| synchronized | 1 |
| descriptors / bit_packets | 30 / 30 |
| iq_packets / iq_bytes | 0 / 0 |
| control_packets | 0 |
| invalid_packets / changed_copies / control_gaps | 0 / 0 / 0 |
| 八路样本计数 | 全部 0 |
| completed_cpi | 0 |
| data_present | false |
| integrity_verified / downstream_tested | false / false |

测试定时退出，容器自动移除，后续检查无残留采集容器或 `/dev/mem` 占用。由于首路无 IQ，没有用重复切换通道或盲目改寄存器掩盖失败，也未执行其余七路采集或下游验收。

## 雷达恢复后的重试

用户说明首次测试时雷达关闭、现在已恢复供数。使用相同镜像、配置、权限和 channel 0 再运行 30 秒，未连接下游，未修改解析实现。[重试原始日志](evidence/capture-channel0-retry1.txt) 单独保留，原失败记录不覆盖。

| 指标 | 重试结果 |
| --- | --- |
| elapsed_s | 30.000118907 |
| descriptors | 205869 |
| iq_packets / iq_bytes | 134302 / 17603231744 |
| control_packets / bit_packets | 71562 / 5 |
| invalid_packets / changed_copies | 0 / 0 |
| control_gaps | 24900 |
| 每路推导样本计数 | 550100992（由四根光纤有效描述字长度推导，不是八路逐样本正确性验证） |
| completed_cpi | 1 |
| discarded_partial_blocks / samples | 24901 / 549349786（包含退出尾块） |
| data_present / receive_errors | true / true |
| 退出码 | 2 |

这证明接收描述字和 IQ 数据路径已活跃。`control_gaps` 当前表示沿用旧适配器的“控制表第 3 个 uint32 应逐包加一”检查失败次数，不等于已证实的硬件丢包数；需核对重复控制表、序号语义及实际采集缺口，不能直接屏蔽该检查后宣称连续性通过。双复制相同也不能证明未发生稳定覆盖。

容器已正常定时退出，无残留采集进程或 `/dev/mem` 占用。未进行其余七路选路验收或下游测试。

## 只提取控制序号后的验证

该次运行依据当时的旧协议，仅从包含 CTRL_HEAD 的载荷偏移 8 读取 W1，不校验其他控制字段或序号变化（当前 [协议文档](../pcie/control-protocol.md) 已更新为双时间戳格式）。最小读取长度及 DMA 边界保护保留，不修改 SDK 契约。

- 本地 CTest 9/9、UBSan 8/8 通过；新增重复、跳号、回退、回绕及短载荷用例。
- 目标 ARM64 镜像构建 CTest 8/8 通过；[构建日志](evidence/docker-build-sequence-observe.txt)。本次归档源码时间戳归一化，未修改主机时钟。
- 镜像：`pcie-source:sequence-observe`，ID `sha256:15d21bddf7cf20f1ed51b5ac866b3b4ffaae0c785af97de8d2772f89bfffc3e0`。保留旧 `pcie-source:dev`，避免覆盖历史对照。

```bash
docker run --rm --name pcie-source-capture \
  --device /dev/mem:/dev/mem --cap-add SYS_RAWIO \
  pcie-source:sequence-observe --capture-only --channel 0 --duration-seconds 30
```

[完整采集日志](evidence/capture-sequence-observe-channel0.txt)，退出码 `0`，未接下游。

| 指标 | 结果 |
| --- | --- |
| elapsed_s | 30.000132828 |
| iq_packets / iq_bytes | 134460 / 17623941120 |
| control_packets / bit_packets | 71940 / 8 |
| invalid_packets / changed_copies | 0 / 0 |
| 最近 control_sequence | 4873778，仅记录不作判断 |
| completed_cpi | 733 |
| 运行中丢弃部分块 | 0 |
| 退出时尾块 | 1 块，114162 个点，按策略丢弃 |
| 选中通道样本计数 | 550748160 = 733 × 751206 + 114162 |
| data_present / receive_errors | true / false |
| integrity_verified / downstream_tested | false / false |

移除控制序号引发的清空后，软件切分持续进行。这证明定长分块路径工作，不证明 DMA 采样从未丢失或覆盖，也不声称其他七路已逐路验收。

## 待完成与阻塞

1. 最新 RX 预期间隔 98304 的 channel 0 短窗口已通过；仍需长时间复测、其余七路选路和已知输入验证。任何额外固件、时序或寄存器变更需先确认。
2. 需要可验证的硬件连续性/覆盖机制，才能保证正式交付帧不是等长但已覆盖的样本。
3. SDK 等待取消需确定支持方式；不允许靠修改 Ring 指针或强制取消线程实现退出。

上述事项解决后继续 T09–T12；不能把当前 channel 0 的观察结果当成八路硬件完整性或端到端通过证明。
