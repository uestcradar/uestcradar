# pcie_source TODO

[实施计划](plan.md) · [需求总览](../README.md) · [验收要求](../tests/README.md)

## 当前任务：RawIQFrame 单通道（已批准）

以 [SPEC](../SPEC.md) 与 [新计划](plan.md) 为准；SignalSink 核心保存实现不修改。

- [x] P1：RawIQFrame 4:1 契约、非对称黄金字节及旧契约回归；本地/原生 SDK 4/4。
- [x] P2：try_create 与端口打开超时；错误/租约/重试、缺失/未初始化端口回归通过。
- [x] P3：原生服务器仿真 Source → 真实 Sidecar → 未修改 SignalSink，ext4 保存 64 帧；逐字节及损坏/截断拒绝通过。开发态测试，不抵扣发布。
- [ ] P4：PCIe 控制与 IQ 配对、硬件帧组装和单通道正式输出。
  - [x] 实现及原生离线接收/组帧/选路测试通过，无 CPI 模板依赖。
  - [ ] 发布候选后验证真实 PCIe 描述字配对与数据。
- [ ] P5：有界输出、背压、故障与安全退出验证。
  - [x] 本地/原生 PCIe 套件 15/15：队列满、SDK 关闭、排空超时、SIGTERM、启动等待等离线检查通过。
  - [ ] 真实 Sidecar 断链/背压及硬件场景复验。
- [ ] P6：不可变镜像交付、服务器真实单通道 10/60 秒录制验收。
  - 原生离线测试和仿真文件已通过；尚未提交/发布新版本，远端发布仓库 HEAD 不一致，不绕过发布运行硬件。

详细证据与残余风险见 [raw-iq-results.md](../tests/raw-iq-results.md)。

## 历史任务与证据（旧 CPI 目标，不作为当前实施要求）

历史状态：独立采集路径已实现，正式输出未实现。用户最新要求以 98304 检查相邻 RX 时间戳；并行双快照优化后，channel 0 的 30 秒实测约 30.693 MS/s、112400 次比较无异常，详见 [性能结果](../tests/throughput-results.md)。其余七路与 DMA 所有权/逐样本正确性未验收，T09 不整体勾选。

最新版本本地 CTest 12/12（含模板差分）、UBSan 12/12、ARM64 镜像 CTest 11/11 通过；协作复制单测通过 ASan+UBSan，TSan 因运行环境映射错误未成功。T04 审查后，接收实现收敛为 `pcie/xdma_rx.h/c`、`pcie/devmem_io.h/c`、`src/pcie_receiver.hpp/.cpp`，不保留旧裸指针队列和线程。T05–T06 原迁移清单需按此调整，且完整性保护仍受阻，暂不勾选为全部完成。路径相对 `pcie_source/`，文件布局为拟定；若单项超出约五个文件，继续拆分，不把整库复制算作一个文件。

## A. 可离线验证的数据路径

- [x] **T01：建立最小构建及八路选路检查**
  - 依赖：无。
  - 文件：`CMakeLists.txt`、`src/iq_decode.hpp`、`tests/iq_decode_test.cpp`。
  - 验收：按四根光纤映射 `{0,1}/{4,5}/{2,6}/{3,7}` 提取 `0..7`；检查掩码、空指针、CS16 对齐和长度；控制表/BIT 不进入 IQ 路径。
  - 验证：计划中的 CMake 构建及 `ctest --test-dir build --output-on-failure`；八路不同序列逐样本核对，不访问设备。

- [x] **T02：加载 signalsource 参数模板，不读取 input.bin**
  - 依赖：T01。
  - 文件：`src/cpi_templates.hpp`、`tests/cpi_templates_test.cpp`、`CMakeLists.txt`。
  - 验收：加载 CPI0..CPI9 的五类参数文件；固定 751206 点、64 脉冲；检查格式、序号、数组和公共配置；缺失及非法参数失败，不自造默认值。
  - 验收：先查找可复用解析入口；若需修改 signalsource 共享代码，按计划边界先确认。保持其现有行为，不复制离线样本加载路径来绕过读取限制。
  - 验证：CTest 使用临时最小参数目录；无 input.bin 仍通过。权威模板与 signalsource 逐字段比对在 T08 镜像数据可用后补验。

- [x] **T03：固定软件 CPI 分块与连续性检查**
  - 依赖：T01、T02。
  - 文件：`src/cpi_assembler.hpp`、`tests/cpi_assembler_test.cpp`、`CMakeLists.txt`。
  - 验收：跨包/一包多帧按 N=751206 分块；完成帧分配递增序号并选择 `CPI[index % 10]`；无补零、无跨缺口拼接；停止丢弃部分块。
  - 验证：CTest 连续至少 11 帧，逐样本检查边界及模板轮转；注入缺口后只接受新可信起点的连续样本。

## B. 底层接收迁移

- [x] **T04：审查最小接收依赖与数据寿命**
  - 依赖：无；实施顺序安排在 T03 后。
  - 文件：`pcie/README.md`、必要时本任务清单。
  - 验收：列出接收调用链、必须迁移的源码/头文件、重复头文件取舍；核对四路覆盖检查、队列竞争、复制期间 DMA 一致性和停机顺序；明确无数据与错误返回。
  - 验证：逐项记录实际函数/来源位置和结论；不能确认数据有效性时标记阻塞项，不声称保证无覆盖。禁止顺带迁移硬件 TX 功能。

- [ ] **T05a：迁移接收定义和寄存器访问**
  - 依赖：T04。
  - 文件：最多五个实际必需文件，初选 `pcie/xdma_lib.h`、`pcie/xdma_fun.h`、`pcie/error.h`、`pcie/devmem_io.h`、`pcie/devmem_io.c`；按 T04 结果收敛。
  - 验收：统一头文件来源，保留必要来源/版权信息；接收路径不依赖开发机绝对路径；映射失败及参数校验明确。
  - 验证：在目标构建环境对 C 源码执行编译检查，无寄存器读写；记录实际编译命令。

- [ ] **T05b：迁移接收队列和线程生命周期**
  - 依赖：T05a。
  - 文件：初选 `pcie/XdmaDataQueue.h`、`pcie/XdmaDataQueue.c`、`pcie/xdmaSimpleApp.c`、`pcie/pspHead.h`、`pcie/XdmaLog.h`；只保留所需部分。
  - 验收：有界队列、索引校验、线程同步、完整停止及队列清理；队列项不被误认为采样副本。
  - 验证：C 编译检查及队列/生命周期代码审查；需要新增独立检查文件时另拆子项，不超过任务文件边界。

- [ ] **T05c：迁移接收解析及必要日志依赖**
  - 依赖：T05b。
  - 文件：初选 `pcie/xdma_fun.c`、`pcie/interface_log.c`、`pcie/interface_log.h`、`pcie/cJSON.c`、`pcie/cJSON.h`；已有依赖可用则复用，不无条件引入新库。
  - 验收：同步描述字、控制表/BIT 分流、覆盖及边界检查落实；不迁移无关发送路径；额外依赖通过 T04 清单显式补任务。
  - 验证：接收路径 C 编译通过；将可提取的非法描述字和边界检查接入后续离线测试，不在编译期间触碰硬件。

- [ ] **T06：底层适配与必要硬件配置落地**
  - 依赖：T05c。
  - 文件：`src/pcie_receiver.hpp`、`src/pcie_receiver.cpp`、`pcie_config/config_sync_4.txt`、`pcie_config/config_drp_4g8.txt`、`pcie_config/log.json`。
  - 验收：统一初始化、接收、有效性检查及清理接口，按 `SYNC_1_4 | DRP_4G8` 工作；配置目录可明确指定；不暴露长寿命 DMA 指针给输出队列。
  - 验证：配置副本与来源比对；检查异常路径、无效地址保护、停止时无消费线程访问已解除映射的区域。实际硬件读写留到 T09。

## C. 首次 Docker 硬件采集，不接下游

- [x] **T07：实现独立采集检查入口**
  - 依赖：T03、T06。
  - 文件：`src/main.cpp`、`src/options.hpp`、`tests/options_test.cpp`、`CMakeLists.txt`。
  - 验收：实现计划中的 CLI；`--capture-only` 完全不创建 SDK 输出对象、不等待 SHM；仍使用真实接收/选路/组帧路径。定时停止、信号停止及初始化失败均安全清理。
  - 验收：每路 IQ 计数、完成 CPI、无效包、覆盖及采集丢包可见；无数据不能因容器存活判成功；检查模式整帧释放单独计数。
  - 验证：CMake/CTest；无硬件 CLI 检查拒绝非法通道及模板，不隐式访问 `/dev/mem`。正式输出尚未实现时明确拒绝该模式，不能静默假装发送。

- [x] **T08：根目录 Dockerfile 与模板打包**
  - 依赖：T07。
  - 文件：`Dockerfile`、`tests/check_image.sh`、`pcie_config/README.md`。
  - 验收：对齐 signalsource 的 ALGO_BASE、固定 CPI_DATA_IMAGE 和镜像标签；入口 `/app/pcie_source`；镜像包含硬件配置及十组参数，不要求 input.bin、不依赖宿主源码。
  - 验证：`docker build --pull -t pcie-source:dev .`；镜像检查不访问设备，确认五类参数文件、入口及架构；与同版 signalsource 数据集逐字段比对模板。记录实际镜像摘要。

- [ ] **T08H：Harbor 发布与目标机 digest 拉取验收**
  - 依赖：T08；构建机 ARM64 构建能力和 Harbor 访问条件就绪。
  - 文件：`pcie_config/README.md`、`tests/hardware-results.md`（发布命令与实际证据）。
  - 验收：在构建机验证镜像，用唯一不可变版本标签 push `registry.chengyistudio.com/cxx/worker`；记录仓库 manifest digest；`.64` 仅按该 digest pull、核对架构及镜像身份，不接收源码或现场编译。
  - 验证：执行配置文档的 Harbor 发布命令与目标机 pull 命令，保存实际输出。不得把本地 image ID 当 manifest digest，或以 save/load 代替 pull。失败则报告 DNS/网络/认证等阻塞。
  - 当前状态：按用户最新指示先跳过，仍未执行，不勾选完成；临时诊断不抵扣此项。

- [ ] **T09：192.162.2.64 上首次独立硬件检查**
  - 当前结果：channel 0 最新双时间戳镜像已复测，30 秒接近标称输入速率、RX 间隔 98304 检查无异常；旧失败日志仍保留。其余七路和 DMA 完整性未验收。
  - 依赖：T08H 正式发布验收按用户要求暂跳过，可继续临时硬件诊断；访问权限、板卡地址及设备占用仍须核对。后续恢复发布验收时，以 Harbor 拉取镜像重新记录结果，临时结果不抵扣该项。
  - 文件：`tests/hardware-results.md`（首次实际执行时创建）。
  - 验收：仅 Docker 采集检查，不启动下游 Worker/Sidecar。先测 channel 0，再分别检查 1..7；有效 IQ 和样本计数持续增长，持续完成完整软件 CPI，内存有界，能正常停止。
  - 验证：执行计划中的最小权限候选命令并记录必要调整；保留每路实际观察时长、镜像摘要、命令、计数和日志。30 秒无数据或不足一帧需明确未通过并诊断，不扩大权限或延长时间后隐去原结果。
  - 关卡：有未解释的无数据、覆盖或解析异常，不标记通过，不推进正式下游验收。

### 新协议适配追加项

- [x] **RX01：控制表改为 TX/RX 两个 64 位时间戳**
  - 替换偏移 8 的旧序号提取；TX 低/高字位于 4/8，RX 低/高字位于 12/16，最小安全长度 20 字节。TX 全 F 不报错。
  - `timestamp_check_test` 验证小端、高字、非对齐和短载荷；`receiver_control_test` 执行真实接收器分流。
- [x] **RX02：相邻 RX 差值检查与完整异常记录**
  - 第一帧建基线，后续 uint64 模差预期按最新要求改为 98304；每帧更新基线，异常不清空 CPI。
  - 默认检查，删除旧序号开关；`--timestamp-errors PATH` 追加全部异常 JSONL，包含前后值、描述字信息和运行标识；无 16 条上限。
  - 单测覆盖正常、重复、前跳、回退、回绕、100 条异常及部分 CPI 不被清空。主循环仅无描述字时休眠，不再因控制包/BIT 休眠。
- [x] **RX03：新镜像 channel 0 独立硬件复测**
  - 用户授权临时目标机构建；初次 8192 阈值测试失败的 [记录](../tests/rx-timestamp-results.md) 保留。
  - 最新 98304 阈值与优化读取镜像，30 秒 112400 次比较无异常、完成 1225 个软件 CPI，全部日志已保留；此项不涵盖八路/逐样本正确性或 Harbor 发布。
- [x] **RX04：读取路径计时、优化与吞吐回归**
  - 分阶段计时确认约 97% 时间消耗在两次 DMA 读取/解码；采用可复用双快照、一个协作线程按半包并行复制，再从普通内存解码并比对。
  - 保留双读取与全部异常记录，不改映射属性、不排队裸 DMA 指针，退出先 join 后解除映射；小包不增加线程唤醒。
  - 对照包含更慢且已撤销的实验；原读取方式复测约 19～21 MS/s，优化后约 30.693 MS/s。`check_capture.py` 核对所有原始记录和 99% 标称吞吐门槛，详见 [证据](../tests/throughput-results.md)。

## D. 正式输出与完整性验收

- [ ] **T10：有界完整 CPI 队列和可退出的 SDK 输出**
  - 依赖：T09。
  - 文件：`src/cpi_output.hpp`、`src/cpi_output.cpp`、`src/main.cpp`、`tests/cpi_output_test.cpp`、`CMakeLists.txt`。
  - 验收：采集持续运行，独立线程写 `Output<IQFrame>`；队列满丢新完成整帧，序号/模板照常前进；明确固定队列容量并记录，存储所有权不重叠。
  - 验收：全部样本填完才提交；输出等待可取消。先核对现有 SDK 能力，遇到 API 限制报告，不使用不安全线程取消或私自改契约。
  - 验证：CTest 用受控阻塞输出检查满队列、恢复、序号间断、内存上限和停机；所有交付帧逐样本验证，不只检查长度。

- [ ] **T11：实际 SDK/Sidecar 链路与背压恢复**
  - 依赖：T10。
  - 文件：`tests/cpi_link_test.cpp`、`tests/run_link_test.sh`、`CMakeLists.txt`、`tests/README.md`。
  - 验收：复用现有 signalsource 链路配置及输入类型；确定性输入核对每帧 Metadata 和完整样本；堵塞下游至 SHM/本地队列满后恢复，仍只交付完整 N 点帧。
  - 验证：新增链路测试接入 CTest，并按脚本在实际 Sidecar 下验证断链/恢复及退出；脚本需记录实际命令，不能用模拟队列通过替代 SDK 阻塞行为验证。

- [ ] **T12：硬件端到端与运行文档收尾**
  - 依赖：T11。
  - 文件：`tests/hardware-results.md`、`tests/README.md`、`pcie_config/README.md`、`README.md`、`tasks/todo.md`。
  - 验收：在构建机重新构建最终镜像并 push Harbor，目标机按 digest pull 后先回归 capture-only，再连接下游验证接口/形状/模板；以已知输入检查八路映射和样本正确性。真实雷达结果不作为假参数模式的验收目标。
  - 验证：记录最终镜像摘要、构建/启动命令、SHM 容量、权限、日志及实际失败项；测试全部通过才勾选。缺少可控输入时明确“已验证有数据，未验证映射/逐样本正确性”，不得算完成全部硬件验收。

## 执行规则

完成任务时记录验证命令与结果。编译、镜像、硬件及链路分别报告，不以某一层通过代替其他层。未实现、未运行和受阻任务保持未勾选；本清单不授权自动提交、远程改系统配置或扩大设备权限。
