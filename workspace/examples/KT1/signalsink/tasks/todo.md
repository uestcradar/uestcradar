# SignalSink Todo

依据：[SPEC](../SPEC.md)、[计划](plan.md)。用户已授权推进实现与测试。源码和局部验证已推进；下列复选框仍按完整验收口径保留，不能用局部结果代替整项通过。进度见 [验证记录](../tests/acceptance.md)。

范围只有 SignalSink 和必要的 Web 专用适配，没有 `worker-control-page` 工作包。
路径以 `signalsink/` 为根；`web/` 指 `workspace/infra/web/`，`sdk/` 指 `workspace/infra/sdk/`。
每项控制在约 5 个源文件以内；前置决策导致范围增加时先拆分并修订计划，不偷偷扩大。

## 统一验证命令

在仓库根目录、已准备相应依赖的原生 ARM 构建环境运行。已执行的环境与结果见验证记录；Worker 必须指向包含 RawFrame 扩展的新 SDK 安装。

```bash
# V-SDK：仅批准 SDK 扩展后使用
cmake -S workspace/infra/sdk -B build/signalsink-sdk -DBUILD_TESTING=ON
cmake --build build/signalsink-sdk --parallel
ctest --test-dir build/signalsink-sdk --output-on-failure

# V-SINK：使用 T01 验证后的 SDK 安装，不误用旧 SDK 镜像
cmake -S workspace/examples/KT1/signalsink -B build/signalsink -DBUILD_TESTING=ON
cmake --build build/signalsink --parallel
ctest --test-dir build/signalsink --output-on-failure

# V-WEB：先生成嵌入资源，再做现有后端/UI 回归
(cd workspace/infra/web/frontend && npm run build && npm test)
(cd workspace/infra/web && go test -timeout 90s ./... && go vet ./...)
(cd workspace/infra/web && CGO_ENABLED=1 go test -timeout 90s -race ./internal/orchestration)
```

## 前置确认

- [ ] **T00 — 审阅 SPEC/Plan/Todo，关闭实施决策门槛**
  - 前置：无；这是后续任务的人工审批门槛，不是代码任务。
  - 验收：逐项确认 D1–D6；明确异常是否继续消费、连续性依据与起录条件、SDK/契约许可、真实保存根目录、缓冲/磁盘预算、文件布局与持久性、采用 SSH 固定 CLI 控制。明确性能初验时长与长期目标。
  - 验证：人工审阅并记录决定；无法确认的项目保留阻塞，不勾选下游任务。用户确认方案方向不等于同意了所有默认参数。
  - 文件：`SPEC.md`、`tasks/plan.md`、`tasks/todo.md`。

- [ ] **T01 — 获得受支持的完整裸帧读取与退出能力**
  - 前置：T00 中 D3 明确批准；若 SDK 维护者另行交付，只验证并记录其接口/版本，不重复实现。
  - 验收：可取得包含 Envelope 的只读完整字节与正确生命周期，保留未知类型数据；支持所需的超时/取消或其他经批准的有界退出方式；旧类型化 API、Ring/传输布局不变。（A2/A3）
  - 验证：V-SDK；原始字节、坏长度、lease 释放、无输入取消及旧接口回归通过；记录可供 Worker 构建使用的 SDK 版本/安装或镜像依据。
  - 文件：按批准范围，候选为 `sdk/include/sdk.h`、`sdk/src/sdk.cpp`、`sdk/src/sdk_test.cpp`、`sdk/src/sdk_interface_test.cpp`、`sdk/README.md`。额外构建/发布改动必须先单列，不能据此直接修改全部 SDK。

## 本地闭环

- [ ] **T02 — 最小文件写入与独立校验**
  - 前置：T01；D5 的文件封装及完成条件冻结。
  - 验收：排他创建、一会话一文件、原始字节不变；正常同步后才能标记完成；短写/中断可正确处理，磁盘满/IO/同步失败保留可识别的未完成前缀。（A2/A4）
  - 验证：V-SINK；IQ/PC/RD/不透明自定义帧逐字节对比，包含保留字段、边界和顺序；截断尾帧和错误完成标记校验失败。
  - 文件：`CMakeLists.txt`、`src/recorder.hpp`、`tests/signalsink_test.cpp`、`tests/check_capture.py`。

- [ ] **T03 — 接收、有限队列与录制状态**
  - 前置：T02；D1/D2/D4 已确认。
  - 验收：默认只消费；起停按确定帧边界串行化；队列连同在途数据有界，不保留失效 lease；状态和磁盘容量反映实际目标；异常严格执行已批准策略。（A1/A3/A5）
  - 验证：V-SINK；重复起停、并发起停、没有后续帧、慢盘、队列满、可检测缺口、磁盘错误、输入中断；超限不静默丢帧后续录，进程正常退出符合已确认期限。
  - 文件：`src/main.cpp`、`src/recorder.hpp`、`tests/signalsink_test.cpp`、`CMakeLists.txt`。

- [ ] **T04 — 同一程序的本地控制子命令**
  - 前置：T03；D6 已确认。
  - 验收：status/start/stop 控制正在运行的同一进程，不打开第二个输入端口；路径不逃逸、不覆盖，选择的目录持久化；迟到 stop 不能影响新录制；控制客户端退出不停止录制。（A1/A4/A7）
  - 验证：V-SINK + SPEC §7 的容器内控制命令；空闲/录制/收尾/失败状态，错误录制 ID、请求断开、超长请求、路径穿越/符号链接、无权限、主进程重启默认不录制。
  - 文件：`src/control.cpp`、`src/main.cpp`、`src/recorder.hpp`、`tests/signalsink_test.cpp`、`CMakeLists.txt`；如果不必拆文件则少建。

## Web 专用适配

- [ ] **T05 — 可靠识别 SignalSink**
  - 前置：T00/D3 确定镜像标识和通用输入契约。
  - 验收：检查结果区分“本地存在 SignalSink 镜像”与“实际运行 SignalSink”；仅经批准的组件标识、契约和运行身份匹配才视为可控目标，普通 sink/相似镜像名不匹配。（A6）
  - 验证：V-WEB；旧 Worker 契约全部保持；未运行、多个候选、运行镜像不符、过期检查结果均不能误控制。
  - 文件：`web/internal/orchestration/types.go`、`contract.go`、`contract_test.go`、`remote.go`、`remote_test.go`（后四项同目录）。

- [ ] **T06 — 具体输入绑定与持久挂载**
  - 前置：T05；D3/D4 已批准。
  - 验收：只给 SignalSink 绑定上游具体 type/version 并生成对应 Sidecar 参数；只给其 Worker 挂载已批准的保存根目录；保留 Sidecar health 依赖与共享 IPC；其他 Worker 的精确类型检查和 Compose 不变。（A2/A4/A6）
  - 验证：V-WEB；规划成功/类型不兼容/容量不足/非法根目录；生成 Compose 经现有校验通过；没有给 Frontend 挂录制卷或改变 strict-RDMA 默认。
  - 文件：`web/internal/orchestration/planner.go`、`planner_test.go`、`types.go`、`remote.go`、`remote_test.go`（同目录）。

- [ ] **T07 — Web 固定操作、SSH 转发和权限测试**
  - 前置：T04、T05。
  - 验收：实现计划中的三个专用 HTTP 入口；复用会话、节点授权、Origin/CSRF、SSH 指纹与安全转义；命令、请求、输出及超时有界；不改 Frontend 代理白名单。（A5/A7）
  - 验证：V-WEB；未登录、无 CSRF、未知节点、身份变化、注入输入、SSH 不可信/超时、大响应、迟到停止、会话失效与 Web 请求取消。取消只结束控制请求，不额外执行停止录制；超时起录查询状态后判定。
  - 文件：`web/internal/orchestration/remote.go`、`http.go`、`signalsink.go`、`signalsink_test.go`、`http_test.go`（同目录；复用现有 fakeRemote）。

- [ ] **T08 — Web 类型和 API 调用**
  - 前置：T07。
  - 验收：三个专用调用复用既有请求函数，处理授权失败和未知结果；大计数不被 JavaScript 截断；请求可取消，无自动重发开始。
  - 验证：V-WEB；方法/路径/CSRF/请求体正确，错误响应、过大计数与取消得到正确结果。
  - 文件：`web/frontend/src/api.ts`、`api.test.ts`、`types.ts`（同目录）。

- [ ] **T09 — 抽屉独立录制区域**
  - 前置：T08。
  - 验收：仅 SignalSink 显示；位于 iframe 外；有目录、起停、真实状态、独立队列水位及真实目标卷水位；忙时禁重复、过期禁操作。其他节点和预览保持原样。（A5/A6/A7）
  - 验证：V-WEB；模拟所有状态、网络中断和慢查询；关抽屉停止轮询但不 stop，重开查真实状态；浏览器在 1280×700、1440×900、2000×1100 下检查控件/预览可用、无溢出或重复滚动回归；按钮标签与状态不能只靠颜色。
  - 文件：`web/frontend/src/RecordingPanel.tsx`、`RecordingPanel.test.tsx`、`App.tsx`、`styles.css`、`frontendEmbedding.test.tsx`（同目录）。

## 部署、验收与交付

- [ ] **T10 — ARM 镜像与无 Web 的单机闭环**
  - 前置：T04、T05；所需 SDK 构建依赖已可用。
  - 验收：按既有 Worker 流程构建镜像并验证已批准标识；无 Web 时以 Worker + Sidecar + Frontend 运行，默认不保存，本地控制可起停；输出在宿主卷持久保留。（A1/A4/A6）
  - 验证：原生 ARM 构建 + V-SINK；Compose 配置检查；复用已发布的测试 Source 跑小数据用例；不运行 Web 仍可消费、预览和控制。重建 Worker 不丢目录配置、不自动续录。若既有发布工具不支持新契约，审批最小适配后再继续。
  - 文件：`Dockerfile`、`compose.yaml`、`README.md`、`tests/check_image.sh`（均按需新增；现有公共发布脚本不在默认修改范围）。

- [ ] **T11 — Web 管理模式、隔离与故障集成**
  - 前置：T06、T09、T10；批准测试节点和磁盘空间，不覆盖未知工作负载。
  - 验收：真实 SSH/容器/文件验证三种操作，断连/关闭/重新打开、Web 重启/会话过期不改变录制；慢盘、空间不足与录制失败符合 D1；无输入时能停止；相邻 Worker 与普通节点抽屉无回归。（A1–A7）
  - 验证：V-WEB + V-SINK；实际浏览器和两种部署形态检查，文件由独立校验器核对；保存前后主链容器 ID/StartedAt、输入计数及资源记录。不能用 HTTP mock 或纯静态页面代替本项。
  - 文件：`tests/integration.sh`、`tests/check_capture.py`、`tests/acceptance.md`；验收数据另列具体产物，不保存凭据。

- [ ] **T12 — 真盘持续吞吐与完整性验收**
  - 当前结果：**八通道压力场景未通过，实际部署目标待确认**。按用户要求提前进行隔离诊断，不代表 T11 已完成。原生 fio：16 GiB、高熵、libaio 深度 32、直接写并完整 CRC 校验；写入耗时 161.28 秒，平均 106.52 MB/s，第 10–120 秒稳定约 101 MB/s，设备利用率 100%。提前回写候选引发录制队列溢出，已撤回。491.52 MB/s 来自草案满配假设，不能代替实际 Source 配置；详情见 [吞吐结果](../tests/throughput-results.md)。
  - 前置：T11；确认目标设备、测试时长、余量及可支配容量。
  - 验收：原生 ARM 受控输入达到 491.52 MB/s 载荷外加框架/文件开销；队列无长期增长、无帧缺失、正常收尾同步成功；单独报告输入、写入、同步及总耗时。（A8）
  - 验证：先运行已批准的初验时长，再扩展长期测试；从实际文件回读校验数量、字节和顺序；记录有/无 Web 轮询时的开销。合成八通道测试不标成真实 PCIe 八通道验收，内存页缓存速度不标成持久化速度。
  - 文件：`tests/throughput.cpp`、`CMakeLists.txt`、`tests/check_capture.py`、`tests/throughput-results.md`。

- [ ] **T13 — 按现有流程发布并以固定 digest 重验**
  - 前置：T12；另行获得发布/测试部署许可。
  - 验收：沿用原生 ARM 构建测试 → Harbor 不可变发布 → 固定 digest 拉取；记录 SignalSink、必要 SDK 和 Web 版本，确认实际运行镜像；README 命令可复现，所有验收项有证据或明确未通过状态。
  - 验证：复用既有发布工具；拉取所记录 digest 后复测起停、持久挂载、文件校验及原预览；仅拉新镜像不算运行版本已切换。保留回退信息，不擅自更新生产链路。
  - 文件：`README.md`、`compose.yaml`、`tests/acceptance.md`、`tasks/todo.md`、`tasks/release.md`；此任务不表示用户已授权本轮发布、部署、commit 或 push。
