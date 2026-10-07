# TODO：独立 Frontend → KT2/KT3 本机验收

状态：**F01 环境准备经用户独立授权已完成，其余 21 项实现/验收任务待执行。** 完成项必须有真实证据。

模块：`frontend-runtime`。依据：[规格 F01–F08](../SPEC-frontend-runtime.md)、[技术计划与验证命令](plan.md)。后续 Web 内嵌和 `.64` 多机验收见文末 G01。

## 执行规则

- 只改 `infra/frontend/`、`infra/web/` 中的功能代码/配置；能力图、规格和本目录为文档例外。
- KT2/KT3 **包括原 Docker 配置也不改**，使用 Frontend 目录内 override。Sidecar、SDK、协议及公共发布脚本不改。
- 所有应用构建、测试、运行均在 ARM64 Docker 内；最终部署从 Harbor 拉取 digest，以 `--no-build` 启动。
- 不新增录制，不新增账号系统，不改算法结果，不自动提交/推送，不影响已有容器。
- 每任务记录实际命令、结果和证据位置，再勾选。若超出约五个源文件，执行前继续拆分，不用目录通配符掩盖大任务。
- 下文功能文件路径相对 `workspace/infra/`；例外文档明确标注。C-ARM、C-CHECK、C-DEPLOY、C-SCOPE 为 plan.md 中完整验证入口，新脚本尚待实现。

## A. 环境与保护

- [ ] **F00 — 建立任务基线和修改范围检查**
  - Depends on：无。
  - Acceptance：保存当前混合工作区状态；保护 KT2/KT3 原配置、源码、GFKD/CSV 指纹，以及 Sidecar/SDK/协议/公共发布脚本；允许目录外的新改动会使检查失败。
  - Verify：运行 C-SCOPE capture；在脚本自测临时目录中制造一次越界修改并确认 verify 非零退出，不改真实案例来测试。
  - Files：`frontend/tests/check_scope.py`；`tasks/evidence/baseline.md`；任务日志目录中的基线清单。

- [x] **F01 — 准备并验证本机 ARM64 容器运行环境**
  - Depends on：宿主变更人工许可；本次依用户独立授权先完成，F00 仍须在应用修改前执行。
  - Acceptance：先取得宿主 QEMU/binfmt 设置变更确认，再处理已知 `exec format error`；ARM64 容器实际执行成功，不使用 AMD64 应用镜像替代。
  - Verify：C-ARM；同时记录 `docker image inspect` 的 `linux/arm64` 和容器内 `aarch64`。失败保留错误，不继续应用运行验收。
  - Files：[环境证据](evidence/arm64-environment.md)；不修改业务源码。
  - Result：已注册 qemu-aarch64，ARM64 基座容器实际返回 `aarch64` / `arm64`，退出码 0；宿主重启后须重新检查。

- [ ] **F02 — 核实 Harbor 镜像与既有发布流程**
  - Depends on：F00、F01。
  - Acceptance：核对 Sidecar、Source、两个 Worker、两个 Sink、Web build-base 的引用、digest、架构与契约；确认已有 GFKD/CSV，不把 Git 忽略当作缺失；不臆造发布标签。
  - Verify：逐一 `docker pull --platform linux/arm64 "$IMAGE"`，随后 `docker image inspect "$IMAGE"`；缓存、拉取成功、应用可运行分开记录。日志不含凭据。
  - Files：`frontend/docker/deploy.env.example`（非敏感配置）；`tasks/evidence/image-inventory.md`。

## B. 独立后端与节点数据

- [ ] **F03 — Frontend Go 配置与 ARM64 检查入口**
  - Depends on：F02。
  - Acceptance：建立 `uestcradar/frontend` module，复用原依赖版本；必须配置 NODE_ID，页面默认 8081，校验监听地址/端口；检查脚本只能使用 ARM64 基座。
  - Verify：C-CHECK backend 的配置单测通过；缺失节点、非法端口必须失败。此时不宣称完整应用可运行。
  - Files：`frontend/go.mod`、`frontend/go.sum`、`frontend/internal/server/config.go`、`frontend/internal/server/config_test.go`、`frontend/docker/check.sh`。

- [ ] **F04 — 迁入既有预览协议实现及测试**
  - Depends on：F03。
  - Acceptance：复用 FlatBuffers 预览服务及原测试，生成类型归属 Frontend module；不修改 `infra/proto`，不改变协议版本、编码、8 MiB 消息上限或丢弃语义。
  - Verify：C-CHECK backend；覆盖原预览测试、短读/短写、坏消息、慢客户端和队列有界性。
  - Files：`frontend/internal/preview/preview.go`、`frontend/internal/preview/preview_test.go`、`frontend/internal/previewfb/preview_generated.go`、`frontend/internal/telemetrypb/telemetry.pb.go`、`frontend/docker/generate-protocols.sh`。

- [ ] **F05 — 单节点绑定与 stream descriptors**
  - Depends on：F04。
  - Acceptance：从 SidecarHello 获取实例与输入/输出类型；绑定一个 NODE_ID，拒绝其他节点；连接替换/断开不残留错误实例状态；可供 `/api/node` 查询。
  - Verify：C-CHECK backend；测试 Source/Operator/Sink、错误节点、错误版本、重连和实例切换，对应 F04。
  - Files：`frontend/internal/preview/preview.go`、`frontend/internal/preview/preview_test.go`、`frontend/internal/preview/node.go`、`frontend/internal/preview/node_test.go`。

- [ ] **F06 — 复用节点遥测 store/hub**
  - Depends on：F03、F04。
  - Acceptance：迁入现有状态缓存、离线判定和遥测 WebSocket 更新逻辑，不依赖 orchestration、SSH 或 Web 会话。
  - Verify：C-CHECK backend；保留原 store/hub 测试并验证离线/恢复；不增加无界历史缓存。
  - Files：`frontend/internal/server/store.go`、`frontend/internal/server/store_test.go`、`frontend/internal/server/hub.go`、`frontend/internal/server/hub_test.go`。

## C. 独立节点页面

- [ ] **F07 — 建立 UI 构建配置**
  - Depends on：F02。
  - Acceptance：沿用 React/TypeScript/Vite 依赖及缓存，不引入微前端框架；资源支持相对路径，构建输出到 Go embed 目录。
  - Verify：在 ARM64 构建容器内核对 lockfile/缓存并检查 TypeScript/Vite 工具可用；完整页面 build 在 F10 验证。
  - Files：`frontend/ui/package.json`、`frontend/ui/package-lock.json`、`frontend/ui/tsconfig.json`、`frontend/ui/vite.config.ts`、`frontend/ui/index.html`。

- [ ] **F08 — 复用波形与预览协议解码**
  - Depends on：F07。
  - Acceptance：保留 IQ/脉压数据的解码、精度、轴和异常输入校验，不修改参考数据或协议；64 位标识不经不安全的 JS number 转换。
  - Verify：C-CHECK ui 中的预览解码单测通过，原有预览测试覆盖不得因迁移丢失。
  - Files：`frontend/ui/src/preview.ts`、`frontend/ui/src/preview.test.ts`、`frontend/ui/src/generated/preview_generated.ts`。

- [ ] **F09 — 复用 RD 与输入/输出预览组件**
  - Depends on：F08。
  - Acceptance：沿用 RD 热力图、输入/输出波形与 fps/drop 展示；订阅仅对应本节点，双 Leg 请求预算保持原行为；关闭页面释放订阅。
  - Verify：C-CHECK ui；RD 编码/池化/坐标测试通过，组件不导入原 Web 的部署或登录逻辑。
  - Files：`frontend/ui/src/rdHeatmap.ts`、`frontend/ui/src/rdHeatmap.test.ts`、`frontend/ui/src/PreviewPanel.tsx`。

- [ ] **F10 — 组成独立节点详情页**
  - Depends on：F05、F06、F09。
  - Acceptance：只调用 Frontend 节点接口；显示节点、可用 Leg、链路/Ring 和真实图像；连接中/无数据/断开明确区分，显示可核对的帧标识；支持根路径与代理前缀。
  - Verify：C-CHECK ui（测试及完整 build）；URL 解析单测覆盖两种路径，缺少 Web 不导致页面请求编排接口。
  - Files：`frontend/ui/src/App.tsx`、`frontend/ui/src/main.tsx`、`frontend/ui/src/styles.css`、`frontend/ui/src/api.ts`、`frontend/ui/src/api.test.ts`。

## D. 应用、镜像与发布

- [ ] **F11 — 接通 HTTP/WS、UDP 遥测和健康检查**
  - Depends on：F03–F06、F10。
  - Acceptance：独立入口连接页面、`/api/node`、快照、`/ws`、`/ws/frames`、`/healthz`；UDP 仅接纳绑定节点；提供 `--healthcheck`，无数据时可就绪但不能伪称预览正常；不启动编排服务。
  - Verify：C-CHECK all；接口、UDP 节点过滤、Origin、子路径、停止/取消和健康检查测试通过。根路径与前缀路径均打开页面检查资源及 WebSocket。
  - Files：`frontend/cmd/frontend/main.go`、`frontend/internal/server/server.go`、`frontend/internal/server/server_test.go`、`frontend/web/assets.go`。

- [ ] **F12 — ARM64 Dockerfile 与基座兼容性**
  - Depends on：F11。
  - Acceptance：复用 ARM64 Web build-base，镜像以非 root 运行 `/frontend`，标注独立 `frontend/v1`；无 SSH/Docker Socket/编译工具运行依赖；必要时只在 web/docker 内调整缓存，原 Web 不回归。
  - Verify：ARM64 容器执行全部测试与镜像 healthcheck；C-CHECK web 回归；`docker image inspect` 显示 ARM64、预期入口和契约。
  - Files：`frontend/Dockerfile`、`frontend/.dockerignore`、`frontend/.gitignore`、`frontend/README.md`；必要时 `web/docker/Dockerfile.build-base`（第五个文件）。

- [ ] **F13 — Frontend 目录内 ARM64 发布入口**
  - Depends on：F12。
  - Acceptance：不改公共 release 脚本；沿用既有源码版本/ARM 发布环境/不可变标签/推送拉回校验/digest 记录规则；缺少验证时不更新滚动标签。
  - Verify：`bash -n workspace/infra/frontend/docker/release.sh workspace/infra/frontend/docker/verify-image.sh`；用入口自测验证错误架构、缺少 label、脏版本和已占用不可变标签时拒绝；这些自测不得真实发布镜像。
  - Files：`frontend/docker/release.sh`、`frontend/docker/verify-image.sh`、`frontend/docker/test-release.sh`、`frontend/docker/README.md`。

- [ ] **F14 — 受控发布 Frontend 并拉回验证**
  - Depends on：F13。
  - Acceptance：用户确认发布源码版本及操作后，在 ARM 发布环境构建/测试/推送；本机拉取同一 digest，ARM64 容器健康检查通过；不能自动提交混合工作区。
  - Verify：按 F13 已审阅入口发布；`docker pull --platform linux/arm64 "$FRONTEND_IMAGE"` 后检查 RepoDigests、架构、契约及启动结果。
  - Files：`tasks/evidence/frontend-release.md`、`frontend/docker/deploy.env.example`（仅非敏感已验证引用）。

## E. 本机真实案例

- [ ] **F15 — KT2 外置 Compose override**
  - Depends on：F14。
  - Acceptance：原两个 Compose 不改；增加三个节点 Frontend，按 plan 端口表覆盖 Sidecar 观测目的地；Worker 指向 Harbor digest；保留算法参数、SHM/IPC 和原健康检查，无 Web/Nginx。
  - Verify：C-DEPLOY 的 KT2 合并配置先 `config --quiet`，检查服务镜像均 ARM64、端口不冲突、无源码挂载；C-SCOPE 通过。
  - Files：`frontend/docker/kt2-infra.override.yaml`、`frontend/docker/kt2-worker.override.yaml`、`frontend/docker/deploy.env.example`。

- [ ] **F16 — KT2 独立 Frontend 实际渲染验收**
  - Depends on：F15。
  - Acceptance：Harbor 拉取并 `--no-build` 启动；算法节点页面显示真实 `1:3` 输入与 `2:2` 输出，原 Worker/Sink 校验通过；连续观察 60 秒，输入输出各至少两个不同 frame_id。
  - Verify：执行 C-DEPLOY KT2，浏览器访问默认 `127.0.0.1:8082`；保存有时间和节点标识的前后图像、帧标识及对应处理日志。其余节点页面分别可用，健康状态不能代替数据验收。
  - Files：`tasks/evidence/kt2-local.md` 及脱敏截图/日志；不改案例文件。

- [ ] **F17 — KT3 外置 Compose override**
  - Depends on：F16。
  - Acceptance：原两个 Compose 不改；四个 Frontend 端口和 NODE_ID 正确；使用真实 GFKD Worker 镜像，原算法、CSV 和结果校验不变，无 Web/Nginx。
  - Verify：先检查 KT2 测试容器已按范围停止，再检查 C-DEPLOY KT3 合并配置；核实 ARM64 镜像/IPC/端口，C-SCOPE 通过。
  - Files：`frontend/docker/kt3-infra.override.yaml`、`frontend/docker/kt3-worker.override.yaml`、`frontend/docker/deploy.env.example`。

- [ ] **F18 — KT3 独立 Frontend 实际渲染验收**
  - Depends on：F17。
  - Acceptance：Harbor 拉取并 `--no-build` 启动；算法节点显示真实 `2:2` 输入和 `3:2` RD 输出，原校验通过；连续观察 60 秒，输入输出各至少两个不同 frame_id。
  - Verify：执行 C-DEPLOY KT3，浏览器访问默认 `127.0.0.1:8083`；检查热力图实际更新及数值/轴语义，保存帧标识、截图和 Worker/Sink 日志，不能用 mock、旧截图或仅 HTTP 200 通过。
  - Files：`tasks/evidence/kt3-local.md` 及脱敏截图/日志。

- [ ] **F19 — 两个案例的旁路故障隔离**
  - Depends on：F16、F18。
  - Acceptance：两个案例分别验证关闭浏览器、慢客户端、Frontend stop/restart；停止 Frontend 30 秒时主链计数继续增长，恢复后 30 秒内重新收到有效预览；数据容器 ID/启动时间不变。
  - Verify：以对应原 Compose + override 操作目标 `frontend-operator` 或 `frontend-rd` 服务；对照原 Worker/Sink 日志和容器检查结果。超时或计数不增长如实失败，不降低门槛。
  - Files：`tasks/evidence/isolation.md`；必要时 `frontend/tests/slow_preview_test.go`（在 ARM64 测试容器中运行）。

- [ ] **F20 — 拉取式复验与修改边界终检**
  - Depends on：F19。
  - Acceptance：只清理本次测试容器，使用已记录环境文件和 digest 重新 pull/up 两个案例，证明不用现场构建；全部应用镜像 ARM64；两个允许目录之外的保护对象未改。
  - Verify：重新执行 C-DEPLOY、C-SCOPE；保存实际解析配置、镜像清单和结果，配置证据必须脱敏。不使用 prune 清理其他镜像/容器。
  - Files：`tasks/evidence/reproducibility.md`、`frontend/tests/check_scope.py`（仅必要修正，不放宽白名单掩盖修改）。

- [ ] **F21 — 交付文档与 F01–F08 汇总验收**
  - Depends on：F20。
  - Acceptance：文档给出准确拉取/启动/访问/停止命令、端口表、已验证 digest、ARM64 模拟要求与已知限制；F01–F08 各有真实证据，无静默跳过。
  - Verify：C-CHECK all/web、C-SCOPE、文档链接检查及 `git diff --check`；审阅所有失败/阻塞项，未解决则不能宣称独立部署完成。
  - Files：`frontend/README.md`、`frontend/docker/README.md`、`tasks/evidence/acceptance.md`、`tasks/todo.md`、`tasks/README.md`。

## 后续审批关卡（不是已批准的实现任务）

- [ ] **G01 — web-frontend-integration：规格与计划审批**
  - 前提：F21 通过。
  - 目标：Frontend 真正页内嵌入 Web；沿用会话、SSH 和全局遥测；通过 `192.162.2.64` 上的 Web 从 Harbor 部署受管节点 ARM64 Docker 链路并验收结果。源码/配置仍限 web、frontend。
  - 产物：`infra/SPEC-web-frontend-integration.md`，审批后追加对应 plan/todo。

- [ ] **G02 — deployment-profiles：部署交付收敛审批**
  - 前提：G01 对应实现和多机验收完成。
  - 目标：整理已验证的本机 TCP/无 Web、多机 RDMA/有 Web 配置及镜像清单，保留显式覆盖；不扩展为新平台或性能压测项目。
  - 产物：`infra/SPEC-deployment-profiles.md`，审批后追加对应 plan/todo。

## 当前阻塞与操作门槛

- **环境阻塞已解除**：ARM64 最小容器探测通过；仅运行时注册，宿主重启后重新验证。尚未运行实际案例。
- **待操作确认**：本次注册之外的宿主修改、发布版本/推送、影响已有容器的操作；推进到 todo 不等于授权这些动作。
- **尚未开始**：Frontend 代码、发布、两个案例运行与 `.64` 多机验收。已有静态 Compose 检查不能抵扣这些任务。
