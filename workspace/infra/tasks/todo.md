# TODO：抽出 Frontend，单机与服务器共用

状态：**frontend-runtime 所有有效任务已完成；G01 尚未完成。** 见 [单机验收](evidence/acceptance.md)。尚无明确硬件阻塞；用户已确认 Web→Frontend 使用 HTTPS，SSH 仅用于部署管理；用户已确认实施；G03–G10 已完成，原生 ARM Go/race/vet、22 项 UI 测试、真实 Frontend/Sidecar 的 HTTPS 浏览器检查通过；内网简化模式已实现并通过 74 项 Go 测试，G11/G12 当前等待可用节点的既有 SSH 访问条件，不宣称整体完成。

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
  - Review gate：HTTPS 方向已确认；用户已确认[计划第 10 节](plan.md) 与 G03–G12 实施，不再重复索要编码审批。最新确认：自行从列表选节点；关闭 Web→Frontend 证书校验，部署自动准备自签证书，不要求 CA/8081 规则。仍不覆盖未知业务或改宿主驱动/防火墙。
  - Acceptance：Web 内嵌同一 Frontend 镜像，保留拓扑、部署、SSH 与会话；全局遥测仍由 Sidecar 直达 Web 9900/UDP，删除旧预览 9901/TCP 监听和旧渲染实现。Web 部署到 192.162.2.64 并管理选定节点；多机默认 strict-RDMA，显式 TCP 可选，禁止静默降级。
  - Verify：真实服务器链路与本机案例使用同一页面/结果契约，检查图像、数值/轴及原算法校验；预览故障不影响主链和 Web 全局管理。不是只给跳转链接，也不新增 RDMA 性能压测。
  - Files：`SPEC-web-frontend-integration.md`，审批后只追加必要的 web/frontend 改动与验收任务。

### G01 的实施子任务（已获实施确认，按项记录结果）

- [x] **G03 — 会话失效通知**
  - Depends on：G01 实施计划审定。
  - Acceptance：Get 超时、Delete、定时 expire 三条路径统一通知取消并保留凭据清零；续期不误取消，重复删除安全，不改现有 TTL/SSH 认证。
  - Verify：现有 session 测试加失效/续期断言，ARM Go test 与 race；这里只建立生命周期通知，不宣称 WS 已完成接入。
  - Files：`web/internal/orchestration/session.go`、`session_test.go`。
  - Result：三条失效路径、续期和重复删除检查通过；见 [G03/G04 测试证据](evidence/web-frontend-tests.md)。

- [x] **G04 — 受保护的 HTTPS/WSS 反向代理**
  - Depends on：G03。
  - Acceptance：复用会话授权，限定已检查节点与固定 HTTPS 8081；拒绝未授权/跨 Origin/路径逃逸，不转发凭据；按内网模式不校验证书但保持加密；绑定会话及请求取消，无 SSH dial 或 HTTP 回退。
  - Verify：ARM Go 集成测试覆盖 HTTP/WS、受信/错误证书、超时、退出与会话失效；慢连接不持有全局管理锁。
  - Files：`web/internal/orchestration/http.go`、`frontend_proxy.go`、`frontend_proxy_test.go`。
  - Result（严格模式历史验证）：HTTP/WSS、TLS 拒绝、身份/路径/凭据隔离、退出/过期/浏览器关闭、超时及管理不阻塞检查通过；包内共 60 个测试/子测试通过，race/vet 通过。随后 G05 已接入节点 snapshot/ws，不转发 Frontend 的空遥测。

- [x] **G05 — 内嵌页面的节点遥测**
  - Depends on：G04。
  - Acceptance：代理前缀的 snapshot/ws 复用 Web Store/Hub，只输出对应节点；全局订阅不变；会话取消释放节点订阅。Sidecar 遥测仍直达 Web 9900。
  - Verify：两节点并发、取消与慢消费者测试，确认全局流不受节点订阅故障影响；不新增 UDP 中转或 JSON 协议。
  - Files：`web/internal/server/hub.go`、`hub_test.go`、`server.go`、`web/internal/orchestration/http.go` 及对应测试。

- [x] **G06 — 节点 Frontend 与 TLS 部署配置**
  - Depends on：G04。
  - Acceptance：现有 Compose/上传验证路径添加固定 digest 的 Frontend；管理 IP HTTPS 8081，预览 localhost:9903；自动生成自签证书并经 SFTP 安装，只读挂载；自动设置 UID/GID 65532、0400 私钥。SSL_CERT_FILE 指向自身证书，不改 Frontend 程序；主链无 Frontend 健康依赖。
  - Verify：生成配置测试、无秘密输出检查；ARM 临时测试证书验证 Frontend TLS 与镜像内 healthcheck，不替换真实部署证书、不改宿主防火墙。
  - Files：`web/internal/orchestration/planner.go`、`planner_test.go`、`remote.go`、`remote_test.go`、必要的 `types.go`。

- [x] **G07 — 显式 transport 选择**
  - Depends on：G06。
  - Acceptance：缺省 strict-RDMA，缺设备仍失败；只有用户显式选择 TCP 才生成 functional/tcp,self 配置，不要求挂 RDMA 设备。UI 明示选择，不能自动降级。
  - Verify：planner 默认值、非法值、缺设备及两类 Compose 测试；UI 选择和请求字段测试。
  - Files：`web/internal/orchestration/types.go`、`planner.go`、`planner_test.go`、`web/frontend/src/App.tsx` 及现有类型/测试调用点。

- [x] **G08 — 详情 iframe 与旧 UI 清理**
  - Depends on：G05、G06、G07。
  - Acceptance：详情内嵌同源 Frontend，保留拓扑/管理/状态；关闭或切换节点卸载 iframe。移除旧 PreviewPanel、预览解码、RD renderer 及其迁移过的测试；不加双实现开关，不改 lockfile 触发基座重建。
  - Verify：ARM UI 测试/构建；检查所有旧组件调用点，确认不删除仍用于管理页的代码。
  - Files：`web/frontend/src/App.tsx`、必要样式，以及旧 `PreviewPanel.tsx`、`preview.ts`、`rdHeatmap.ts` 与相应测试。

- [x] **G09 — 移除 Web 旧预览后端**
  - Depends on：G08。
  - Acceptance：删旧 preview 接收/转发、生成类型及旧全局 /ws/frames，Web 不再监听 9901；保留 8080 管理入口及 9900 全局遥测，Frontend 自己的 /ws/frames 不受影响。
  - Verify：ARM Go/UI 全量构建测试、端口/路由断言、旧预览引用检查；不发布中间双实现状态。
  - Files：`web/internal/server/server.go` 及测试、旧 `web/internal/preview/`、`web/internal/previewfb/`、旧 UI preview 生成类型。

- [x] **G10 — ARM HTTPS 集成验证**
  - Depends on：G09。
  - Acceptance：使用同一 Frontend 镜像，实际验证同源前缀 assets/API/WS、无 CA 的加密连接、节点遥测隔离与会话关闭；Web 其他管理测试回归通过。测试 CA 只用于测试。
  - Verify：plan 第 10 节原生 Docker 命令及 ARM race；浏览器验证。不得据此宣称真实服务器网络/TLS 规则已通过。
  - Files：沿用上述测试位置，结果写入 [集成测试证据](evidence/web-frontend-tests.md)。
  - Result：73 个 Go 测试/子测试、race/vet、22 个 UI 测试通过；原 Harbor Frontend 的 TLS healthcheck/assets/API/WSS 通过。真实 Chrome 使用隔离 CA 信任，显示同源 iframe、真实 Sidecar Hello 与已连接预览通道，无 TLS 绕过。浏览器夹具的会话/探查/遥测为测试替身，没有算法帧，不能抵扣 G12。

- [ ] **G11 — 发布与服务器部署准备**
  - Depends on：G10、从已有列表探查可登录节点；已有业务覆盖仍须确认。无证书/CA/白名单前提。
  - Acceptance：已确认源码、未占用版本、ARM 测试后经既有流程发布 Web；核对 Frontend digest、自动证书/权限、本地健康检查及 HTTPS/WSS；不更改宿主防火墙/驱动。
  - Verify：Harbor push/pull/inspect 及只读环境核验；记录缺失项，不隐藏权限或配置门槛。
  - Files：现有 Web 操作说明、`tasks/evidence/web-frontend-release.md`。
  - Pending：旧 TLS/CA/白名单门槛已取消。[当前探查/自动证书证据](evidence/web-lan-mode.md)：仅 .64 可用当前 SSH 登录；5 台认证失败、3 台 No route to host。需要现有 SSH 访问条件，不是手工安全配置或已证实的硬件故障。源码尚未提交/发布。

- [ ] **G12 — Web 管理的真实服务器验收**
  - Depends on：G11、部署操作授权。
  - Acceptance：通过 .64 Web 部署已选 KT2/KT3 服务器链，内嵌真实输入输出，原校验通过；≥60 秒且每 Leg ≥2 帧；Frontend 停 30 秒主链与 Web 全局状态继续、30 秒内恢复，主链不重启；记录实际 transport。
  - Verify：同一次服务器运行完成绘图、运输方式与隔离验收；按规格逐项记录，保护范围复核。全部通过后才关闭 G01。
  - Files：`tasks/evidence/web-server-acceptance.md` 与必要截图/日志、任务状态和部署说明。

编号保留以便追溯：F13 发布工具链取消；F19/F20 的隔离/拉取复验并入 F16/F18，范围终检归 F21；G02 的部署说明归各阶段收尾，不再单列模块。这些是删并任务，不是已完成实现。
