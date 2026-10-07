# Capability Map：节点 Frontend 分离与统一部署

状态：**按用户要求，frontend-runtime 已形成 Plan/Tasks 待审清单；未开始实现。**

架构依据：[TARGET_ARCHITECTURE.md](../TARGET_ARCHITECTURE.md)。
流程入口：[tasks/README.md](tasks/README.md)。最新功能修改范围仅为 `workspace/infra/web/` 和 `workspace/infra/frontend/`；KT2/KT3 全部原文件只读，使用 Frontend 目录内的外置 Compose override。能力图、规格和 tasks 为文档例外。

## 已明确的方向

- 节点部署单元为 Worker + Sidecar + Frontend，Frontend 可独立访问、升级和回滚。
- 构建、测试和部署的目标镜像统一为 **linux/arm64**，包括 x86 本机验收，不新增 AMD64 应用镜像作为替代。
- Frontend 是节点预览页面及必要的轻量后端，不只是静态资源目录。
- Web 保留全局拓扑、链路部署、跨节点管理与节点入口聚合，不退化为纯微前端外壳。
- 单机默认 UCX functional / tcp,self，不启动 Web；多机默认 strict-RDMA，启动 Web。
- 两种部署共用模块、接口和配置格式；允许单机显式开启 Web、多机显式使用 TCP。strict-RDMA 校验失败不得静默降级。
- 默认不额外部署 Nginx；Frontend/Web 停止或预览拥塞不得回压主数据面。

## 本轮明确的交付顺序与限制

1. **先本机独立部署**：不开启 Web，分别跑通 KT2 脉压与 KT3 RD 案例，浏览器显示真实链路结果，而非静态页面或模拟预览。
2. **再多机嵌入集成**：把同一 Frontend 页面嵌入部署在 `192.162.2.64` 的 Web，通过该 Web 在选定服务器上拉取镜像、部署多机链路并展示节点结果；这是本期多机验收入口。Web 所在机器不等于 Worker/Sidecar 的部署机器；不要求每台机器再运行一份 Web，仅提供跳转链接不算完成。
3. 本期仅迁移现有能力，不新增录制、算法或 SDK 输出能力；保留既有结果校验。
4. 范围进一步收紧：**KT2/KT3 连原 Docker 配置也不改**；所有功能、测试、构建发布配置只在 web/frontend 两目录修改。Sidecar、SDK、协议和公共发布脚本只读，越界需求先说明。
5. 开发测试之后的单机与服务器部署，尽可能从 Harbor 拉取已发布镜像并固定 manifest digest；启动时禁止隐式构建。缺少镜像则先完成受控构建、测试和发布，不在部署现场临时构建来冒充可复现部署。
6. 优先复用 Go、React/TypeScript 及现有预览协议，不引入微前端框架，不重写 UCX、SDK、Ring 或帧契约。
7. Frontend 拟放在 `infra/frontend/`，默认共用镜像、每实例绑定节点身份；现有 `infra/web/frontend/` 保留聚合管理页面，不能整体搬走。

## 能力划分

模块 ID 用于规格、计划与任务引用，不代表必须建立同名源码目录。

| Module id | 职责与可独立验收的结果 | Depends on |
|---|---|---|
| frontend-runtime | 提供独立节点 Frontend 镜像与接口；通过外置 override 接入只读 KT2/KT3，在本机 TCP、无 Web 环境展示真实脉压与 RD 结果 | — |
| web-frontend-integration | 将同一 Frontend 嵌入 Web，保留全局管理；通过 192.162.2.64 上的 Web 部署与验收多机 Docker 链路 | frontend-runtime |
| deployment-profiles | 整理两阶段验证过的可复用部署配置与 Harbor 镜像清单，复验拉取部署、显式覆盖和生命周期隔离 | frontend-runtime、web-frontend-integration |

建设顺序：**frontend-runtime → web-frontend-integration → deployment-profiles**。本机 KT2/KT3 是第一模块的完成条件，服务器集成是第二模块的完成条件，不能推迟到最后才验证；Harbor 拉取原则从第一模块镜像部署验证起适用。

模块间提供者接口写入提供者规格：Frontend 的节点身份、入口、版本、预览接口及状态查询契约归 `frontend-runtime`；Web 的集成和管理行为归 `web-frontend-integration`；部署配置引用这些契约，不另定义第二套协议。依赖是开发顺序，不代表 Frontend 运行时依赖 Web。

## 现有代码落点

- `web/frontend/src/App.tsx`：当前聚合界面直接嵌入节点详情和 `PreviewPanel`。
- `web/frontend/src/PreviewPanel.tsx`：现有预览展示入口。
- `web/internal/preview/`：现有预览接收后端。
- `web/internal/server/server.go`：当前同时启动编排、遥测和预览，注册 `/ws/frames`。
- `web/internal/orchestration/`：应保留在 Web 的部署与管理逻辑。
- `sidecar/main.cpp`：现有 `TELEMETRY_HOST`、`PREVIEW_HOST`、`PREVIEW_PORT` 配置。拆分前须明确数据去向，不能仅移动页面文件。

以上只用于识别范围，不是已批准的修改清单。

## 已发现的约束与后续问题

- 本机及 Docker 引擎为 x86_64，所有目标镜像仍固定 `linux/arm64`。已依用户授权用一次性宿主架构安装工具注册 qemu-aarch64，ARM64 基座容器实际执行通过，原 `exec format error` 阻塞解除。见 [环境证据](tasks/evidence/arm64-environment.md)；宿主重启后需重新检查注册。
- 已核对 [Web 构建基座](web/docker/README.md)、[Sidecar 双基座](sidecar/docker/README.md) 和正式发布脚本：复用 ARM64 build-base/runtime-base，按既有受控流程构建、测试、推送 Harbor，验收端拉取运行；Frontend 拆分不另设 AMD64 发布路线；独立发布入口放 frontend/docker，不修改公共发布脚本。
- 两个 infra Compose 大多已使用 Harbor digest；两个 worker Compose 仍有 `build:` 和本地 `:dev` 标签，不能误称现有配置全是拉取部署。
- **KT3 编译产物已存在**：`examples/KT3/algorithm/GFKD_V1_ARM` 为 ARM aarch64 ELF，394352 字节，权限 755，四份 subband filter CSV 同在目录中。它被 `.gitignore` 排除，不等于本地缺失；Dockerfile 会复制至 `/app/algorithm/`，无需 GFKD 源码。
- 本地已有 ARM64 `worker:rd-algorithm-v1.0.0`，缓存记录的 Harbor digest 为 `sha256:0213dfc739c7ca3ca6f689e03ac02a2a2ad29f6ac74a85a5b38f77bf1d89e00a`；后续仍需实际拉取并验证，不把缓存元数据当作本轮发布或部署通过。
- Web 已有 SSH 会话、Cookie/CSRF 与主机指纹确认机制。嵌入沿用既有机制，SSH 密码/私钥不传给 Frontend，不新增登录、SSO 或跨机凭据系统作为本期前置需求。
- Web 继续管理远端 Docker、读取全局状态；Frontend 关闭只影响该页面预览，不应停掉数据链或 Web 的部署管理。具体端口/数据连接在计划中由代码现状确定，不要求用户额外设计一套观测系统。
- `192.162.2.64` 是多机 Web 的部署与验收入口，Web 通过现有 SSH/Compose 机制部署其他节点。验收记录实际节点、镜像与 transport；不另行新增 RDMA 性能压测作为本期要求。

## 规格索引

按模块逐一执行 Specify → Plan → Tasks → Implement，每阶段均需人工确认：

- [SPEC-frontend-runtime.md](SPEC-frontend-runtime.md)：当前范围基线；对应 [plan.md](tasks/plan.md) 和 [todo.md](tasks/todo.md) 已生成待审。
- `SPEC-web-frontend-integration.md`：后续编写。
- `SPEC-deployment-profiles.md`：后续编写。

各规格与本能力图并列保存；本轮按用户要求整理当前模块的计划与任务草案，并未实施。后续模块仍逐一审定规格、计划和任务，使用稳定模块 ID 标识归属。
