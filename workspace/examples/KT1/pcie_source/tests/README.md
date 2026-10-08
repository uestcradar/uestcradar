# RawIQFrame 验证

[规格](../SPEC.md) · [当前结果](raw-iq-results.md) · [历史采集记录](hardware-results.md)

本轮目标只是一条选中通道到 SignalSink 的完整裸帧文件，不验收八路同时输出，不接算法。SignalSink 核心和保存格式不修改。

## 离线构建

从仓库根目录执行，先安装新 SDK，再构建仿真 Source 和 PCIe Source：

```bash
cmake -S workspace/infra/sdk -B /tmp/rawiq-sdk-build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/tmp/rawiq-sdk-install
cmake --build /tmp/rawiq-sdk-build --parallel
ctest --test-dir /tmp/rawiq-sdk-build --output-on-failure
cmake --install /tmp/rawiq-sdk-build
cmake -S workspace/examples/KT1/signalsource_raw_iq -B /tmp/rawiq-source-build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/tmp/rawiq-sdk-install
cmake --build /tmp/rawiq-source-build --parallel
cmake -S workspace/examples/KT1/pcie_source -B /tmp/rawiq-pcie-build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/tmp/rawiq-sdk-install \
  -DRAWIQ_SOURCE_BINARY=/tmp/rawiq-source-build/signalsource-raw-iq
cmake --build /tmp/rawiq-pcie-build --parallel
ctest --test-dir /tmp/rawiq-pcie-build --output-on-failure --timeout 15
```

SDK 检查黄金字节、动态矩阵、非法尺寸、两种端口打开超时、满 Ring、租约取消、关闭和损坏。PCIe 检查独立通道映射、真实 receiver 的控制提取、组帧边界、背压/溢出/排空、SIGTERM 和缺失 SHM。所有这些检查不访问真实设备。

Worker 单目录 Docker context 不含框架 Ring 源码，故 Ring 输出测试和离线校验器在完整仓库构建；不要把镜像内部较少的测试项当成完整离线回归。

## 仿真经真实 Sidecar 落盘

`compose.raw-iq.yaml` 显式使用 TCP，四个测试容器，不改现有 Web/Frontend。启动接收端并开启录制后才启动有限帧 Source；保存 64 帧，每帧 8192 个单通道样本。

```bash
export SIDECAR_IMAGE='registry.chengyistudio.com/cxx/sidecar@sha256:实际摘要'
export SIGNALSINK_IMAGE='registry.chengyistudio.com/cxx/worker@sha256:实际摘要'
export RAWIQ_SOURCE_IMAGE='registry.chengyistudio.com/cxx/worker@sha256:实际摘要'
python3 workspace/examples/KT1/pcie_source/tests/raw_iq_pipeline.py \
  --checker /tmp/rawiq-pcie-build/raw-iq-check --evidence /path/to/new-evidence-directory
```

必须替换示例摘要。脚本要求 Docker Compose v2；默认捕获目录在证据目录下。`--capture-root` 可指定已单独创建的 Docker 主机目录（远端 Docker 尤其需要）；拒绝 tmpfs/ramfs。宿主机先确认监听端口 43637 和诊断端口 44322～44325 无冲突，远端 Docker 还需检查远端端口，不只检查客户端。

脚本核对控制进程身份、帧数、顺序、TX/RX 和所有 IQ 字节，注入 IQ/时间戳损坏及截断验证拒绝，保存结果和日志后删除自己的容器。正常文件和证据保留；不会删除其他容器或用户录制。运行中固定 PID/image 不代表正式发布通过；开发挂载、QEMU 与架构信息必须另行记录。

旧 Docker 19.03/API 1.40 不支持显式 platform 创建参数。仅在确认所有镜像和主机均为原生 ARM 后，可用测试 override 的 `platform: !reset null` 移除平台参数；不升级守护进程、不改变架构验证规则，也不自动进行协议回退。

## 文件校验

```bash
/tmp/rawiq-pcie-build/raw-iq-check capture.sink --fixture --frames 64
# 真实采集：N、FNV 来自同一有限会话 Source 的最终 [raw-iq] 行
/tmp/rawiq-pcie-build/raw-iq-check capture.sink --frames N --digest FNV
python3 workspace/examples/KT1/signalsink/tests/check_capture.py capture.sink
```

校验器使用 SDK 自身解码 RawIQFrame，不复制 Metadata 偏移。要求本次文件从 Source 帧序号 1 开始、只有一个 Source 会话；因此必须先录制再启动 Source，不用该命令误判任意中途录制的文件。真实数据不能使用 `--fixture`。

业务 FNV-1a64 是普通链路诊断指纹，不是密码学证明；通用 SignalSink 校验器另给完整裸帧 SHA-256。指纹相同不证明硬件从未漏采。

## 发布后的真实 PCIe 验收（待执行）

1. 固定新 SDK、PCIe Worker、SignalSink 与 Sidecar 的实际发布 digest，核对原生架构、端口契约 4:1、设备占用和权限。
2. 单独运行 10 秒 `--capture-only`，确认描述字与 control→8192 点 IQ 的关系、所选通道及接收错误计数。不以历史 CPI 日志替代。
3. 开启 SignalSink 录制，启动所选一路的有限时长正式 Source；先 10 秒冒烟，再 60 秒录制。
4. Source 正常结束，核对零可检测异常；等待 Sink 保存量达到 Source 提交量，停止录制并等待同步完成。
5. 将 Source 业务指纹/样本量/帧数与文件、Footer、帧序相互核对；保留日志、文件摘要、镜像和存储信息。
6. 断链、背压等故障单独验收。Sidecar 既有策略可能丢整帧，任何数量/顺序差异都应失败，不宣称自动恢复后无损。

60 秒窗口完整保存与长期稳态写入分开。按本轮真实单通道输入速率测量，不用八通道假设，也不将写缓存吞吐当成持续磁盘带宽。构建发布或硬件配对关卡未通过时，停止，不绕过。

`check_capture.py`（本目录）及旧 CPI 相关测试保留给历史记录；新 USINK001 文件使用上面的 `raw-iq-check` 和 SignalSink 通用检查器。
