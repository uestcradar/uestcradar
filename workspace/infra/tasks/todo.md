# TODO：抽出 Frontend，单机与服务器共用

状态：**frontend-runtime 所有有效任务已完成；G01 尚未完成。** 见 [单机验收](evidence/acceptance.md)。尚无明确硬件阻塞；Web 集成规格已形成待审，按原定审定门槛等待确认方案与服务器清单，不宣称整体完成。

依据：[规格 F01–F08](../SPEC-frontend-runtime.md)、[计划](plan.md)。以下细化 `frontend-runtime`；G01 的 Web 集成是整体目标必做项，不是可选扩展。

执行规则：遵守 plan 第 1 节范围；所有应用构建/测试/运行均为 ARM64；先记录证据再勾选。不自动提交、发布或替换已有容器。文件路径相对 `workspace/infra/`；任务若实际超出约五个源文件，实施前继续拆分。

## A. 准备

- [x] **F00 — 记录保护基线**
  - Depends on：无。
  - Acceptance：记录起点工作区与已有差异、GFKD/CSV 哈希；后续只允许计划范围内的新增修改，不覆盖原基线。
  - Verify：C-SCOPE 采集部分；用 Git 审阅已有差异，不开发检查工具。
  - Files：验收记录目录中的 Git 状态/差异与哈希；不改业务文件。

- [x] **F01 — 本机 ARM64 环境**
  - Depends on：用户已单独授权。
  - Acceptance：ARM64 容器实际运行，不以 AMD64 应用镜像替代。
  - Verify：已有固定基座实测返回 aarch64/arm64；宿主重启后重新探测。
  - Files：[环境证据](evidence/arm64-environment.md)。仅环境完成，不代表案例通过。

- [x] **F02 — 核实现有镜像与数据**
  - Depends on：F00、F01。
  - Acceptance：确认所需 Sidecar、Source、Worker、Sink 与 Web build-base 的 Harbor 引用、digest、ARM64 架构；保留已存在的 GFKD/CSV，不重建无关组件。
  - Verify：对候选镜像执行 docker pull/image inspect，区分缓存信息与实际验证；缺镜像走已有发布流程，不现场构建冒充部署。
  - Files：[镜像拉取/架构记录](evidence/image-inventory.txt)，说明见应用验证证据。

## B. 抽出节点后端

- [x] **F03 — 最小进程配置**
  - Depends on：F02。
  - Acceptance：建立独立 Go module，复用依赖；NODE_ID 必填，监听地址/端口可配置并校验，不导入编排模块。
  - Verify：在 ARM64 构建容器中运行配置测试；F12 纳入 Dockerfile test 阶段。不新增测试包装脚本。
  - Files：`frontend/go.mod`、`frontend/go.sum`、`frontend/internal/server/config.go`、`frontend/internal/server/config_test.go`。

- [x] **F04 — 迁入已有预览服务**
  - Depends on：F03。
  - Acceptance：迁入实现、测试与生成类型；协议、8 MiB 限制、队列有界和丢弃语义不变，不新增协议生成工具链。
  - Verify：ARM64 容器中运行迁入的 Go 预览测试，保留坏消息、短读/写及慢消费者检查。
  - Files：`frontend/internal/preview/preview.go`、`frontend/internal/preview/preview_test.go`、`frontend/internal/previewfb/preview_generated.go`、`frontend/internal/telemetrypb/telemetry.pb.go`。

- [x] **F05 — 绑定单节点并读取已有 stream 描述**
  - Depends on：F04。
  - Acceptance：仅接纳指定 NODE_ID；从 SidecarHello 读取实例和输入/输出类型，正确处理断开与替换；不新增节点注册中心。
  - Verify：在原预览测试中补节点过滤与重连检查，供 /api/node 使用。
  - Files：`frontend/internal/preview/preview.go`、`frontend/internal/preview/preview_test.go`。

- [x] **F06 — 复用节点遥测**
  - Depends on：F03、F04。
  - Acceptance：复用 store/hub、离线判定和 WebSocket 状态，不新建遥测协议或历史数据库。
  - Verify：ARM64 容器中运行迁入的 store/hub 测试；节点停止/恢复状态正确。
  - Files：`frontend/internal/server/store.go`、`frontend/internal/server/store_test.go`、`frontend/internal/server/hub.go`、`frontend/internal/server/hub_test.go`。

## C. 迁入同一套节点页面

- [x] **F07 — 复用 UI 构建配置**
  - Depends on：F02。
  - Acceptance：沿用 React/TypeScript/Vite 与 lockfile，不升级依赖；产物供 Go embed，资源支持代理前缀。
  - Verify：核对现有 ARM64 基座依赖；完整构建在 F10/F12 验证，不提前重做基座。
  - Files：`frontend/ui/package.json`、`frontend/ui/package-lock.json`、`frontend/ui/tsconfig.json`、`frontend/ui/vite.config.ts`、`frontend/ui/index.html`。

- [x] **F08 — 迁入波形与预览解码**
  - Depends on：F07。
  - Acceptance：沿用编码、精度、轴和异常输入校验；64 位标识不经过不安全的 JS number 转换。
  - Verify：ARM64 容器中运行原 preview 测试，不改算法参考数据。
  - Files：`frontend/ui/src/preview.ts`、`frontend/ui/src/preview.test.ts`、`frontend/ui/src/generated/preview_generated.ts`。

- [x] **F09 — 迁入 RD 与预览组件**
  - Depends on：F08。
  - Acceptance：保留已有绘图、数值/轴、fps/drop、双 Leg 预算与取消订阅语义；不重做样式或另造单机渲染器。
  - Verify：运行原 RD 测试；组件无 Web 部署/登录依赖。
  - Files：`frontend/ui/src/rdHeatmap.ts`、`frontend/ui/src/rdHeatmap.test.ts`、`frontend/ui/src/PreviewPanel.tsx`。

- [x] **F10 — 独立节点页面**
  - Depends on：F05、F06、F09。
  - Acceptance：用节点接口驱动同一 PreviewPanel，显示链路/Ring、真实帧及等待/离线状态；根路径和内嵌代理前缀共用页面。
  - Verify：ARM64 UI 测试/构建；补必要 URL 检查，无 Web 时不请求编排接口，不把旧帧标为实时。
  - Files：`frontend/ui/src/App.tsx`、`frontend/ui/src/main.tsx`、`frontend/ui/src/styles.css`、`frontend/ui/src/api.ts`、`frontend/ui/src/api.test.ts`。

## D. 接通应用并发布

- [x] **F11 — 最小 HTTP/WS 进程**
  - Depends on：F03–F06、F10。
  - Acceptance：接通 /api/node、原快照与 WS、静态页及 /healthz；UDP 仅接纳绑定节点；支持镜像内健康探测，不启动 Web 编排。
  - Verify：ARM64 Go 测试覆盖节点过滤、Origin、根路径/代理前缀和停止；就绪与数据到达分开判断。
  - Files：`frontend/cmd/frontend/main.go`、`frontend/internal/server/server.go`、`frontend/internal/server/server_test.go`、`frontend/web/assets.go`。

- [x] **F12 — ARM64 应用 Dockerfile**
  - Depends on：F11。
  - Acceptance：复用现有 build-base；test 阶段跑 Go/Vitest 与 UI 构建，运行镜像以非 root 启动 /frontend，不含 SSH/Docker Socket 或编译工具；不另建发布契约体系。
  - Verify：C-CHECK；用 docker image inspect 和健康探测检查运行镜像。只有实际发现基座问题才安排必要修正。
  - Files：`frontend/Dockerfile`、`frontend/.gitignore`、`frontend/README.md`。

- [x] **F14 — ARM 构建测试、发布 Harbor、本机拉回**
  - Depends on：F12、用户发布许可。
  - Authorization：用户已明确允许提交并推送 upstream/feat/signalsink、发布对应 Frontend 镜像；先固定源码版本，再推送镜像，不覆盖已有版本。
  - Acceptance：明确源码版本、确认标签未占用；ARM64 测试通过后用原生 Docker 命令推送不可变版本，本机拉取同一 digest 并运行；不新增发布器或滚动标签自动化。
  - Verify：按 README 记录 build/test/push/pull/image inspect 命令、退出码、源码版本与 digest；标签检查的认证/网络错误必须阻塞发布。
  - Files：`frontend/README.md`（操作步骤）、`tasks/evidence/frontend-release.md`。

## E. 一次跑完每个单机案例

- [x] **F15 — KT2 单一 Compose**
  - Depends on：F14。
  - Acceptance：根目录一份 compose.yaml、一个 project 包含完整链路和三个 Frontend；内置 ARM64 Harbor digest、端口与 TCP 默认值，无 build/Web/额外环境文件。Worker 使用 ipc: service:sidecar-b 与健康依赖，数据链不依赖 Frontend；删除两份旧配置。
  - Verify：无额外业务变量时 config --quiet 通过；检查默认镜像、IPC/依赖与端口，运行验证留给 F16。
  - Files：`../examples/KT2/compose.yaml`（新增）、`../examples/KT2/docker-compose-infra.yaml`（删除）、`../examples/KT2/docker-compose-worker.yaml`（删除）、`../examples/KT2/README.md`（部署说明）。

- [x] **F16 — KT2 真实结果、拉取部署与隔离验收**
  - Depends on：F15。
  - Acceptance：默认一条 up -d --no-build 可启动；原校验通过，页面显示真实 1:3 → 2:2；满足 plan 第 7 节的 60 秒/有效帧与 30 秒停止恢复要求。
  - Verify：一次 C-DEPLOY 同时完成 Harbor 复验、实际浏览器绘图与旁路隔离；访问 127.0.0.1:8082，其余节点页面也可用。
  - Files：`tasks/evidence/kt2-local.md` 与必要截图/日志，不改案例算法或测试。

- [x] **F17 — KT3 单一 Compose**
  - Depends on：F16。
  - Acceptance：根目录一份 compose.yaml、一个 project 包含真实 GFKD 链路和四个 Frontend，默认无需额外配置；RD Worker 用 ipc: service:sidecar-rd-bridge，其他 Worker 共享各自 Sidecar IPC；删除旧配置，数据链不依赖 Frontend。
  - Verify：KT2 本次测试已停止；检查 ARM64 digest、TCP 默认值、IPC/健康依赖、端口与无 build/Web；运行验证留给 F18。
  - Files：`../examples/KT3/compose.yaml`（新增）、`../examples/KT3/docker/docker-compose-infra.yaml`（删除）、`../examples/KT3/docker/docker-compose-worker.yaml`（删除）、`../examples/KT3/README.md`（部署说明）。

- [x] **F18 — KT3 真实结果、拉取部署与隔离验收**
  - Depends on：F17。
  - Acceptance：默认一条启动命令可用；原校验通过，页面显示真实 2:2 → 3:2 RD；满足同一套有效帧与旁路隔离门槛。
  - Verify：一次 C-DEPLOY；访问 127.0.0.1:8083，核对实际热力图更新、数值/轴语义及 Worker/Sink 结果，不用 mock 或 HTTP 200 替代。
  - Files：`tasks/evidence/kt3-local.md` 与必要截图/日志。

- [x] **F21 — 单机阶段收尾**
  - Depends on：F16、F18。
  - Acceptance：用已有测试和案例证据核对规格 F01–F08；部署说明只要求单一 Compose 入口，开发验收另列；确认保护范围未越界，明确整体还需 G01。
  - Verify：C-SCOPE 检查部分、文档链接与 git diff --check；不重复部署或新建“交付审计”工具，失败/未执行项不得勾选通过。
  - Files：`frontend/README.md`、`tasks/evidence/acceptance.md`、`tasks/todo.md`、`tasks/README.md`。

## F. 必须完成的 Web 集成

- [ ] **G01 — 审定 Web 集成规格，再实现服务器同页验收**
  - Depends on：F21；集成规格审定后细化实现任务，不提前改服务器。
  - Review gate：[Web 集成规格](../SPEC-web-frontend-integration.md) 待审；需确认同源 SSH 代理方案及目标服务器，不是硬件阻塞。
  - Acceptance：Web 内嵌同一 Frontend 镜像，保留拓扑、部署、SSH 与会话；全局遥测仍由 Sidecar 直达 Web 9900/UDP，删除旧预览 9901/TCP 监听和旧渲染实现。Web 部署到 192.162.2.64 并管理选定节点；多机默认 strict-RDMA，显式 TCP 可选，禁止静默降级。
  - Verify：真实服务器链路与本机案例使用同一页面/结果契约，检查图像、数值/轴及原算法校验；预览故障不影响主链和 Web 全局管理。不是只给跳转链接，也不新增 RDMA 性能压测。
  - Files：`SPEC-web-frontend-integration.md`，审批后只追加必要的 web/frontend 改动与验收任务。

编号保留以便追溯：F13 发布工具链取消；F19/F20 的隔离/拉取复验并入 F16/F18，范围终检归 F21；G02 的部署说明归各阶段收尾，不再单列模块。这些是删并任务，不是已完成实现。
