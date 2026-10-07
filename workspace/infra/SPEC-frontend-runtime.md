# Spec: 独立节点 Frontend 与 KT2/KT3 本机验收

- Module id：`frontend-runtime`
- 状态：**单机阶段已完成：ARM 构建/测试、Harbor 发布及 KT2/KT3 真实绘图/隔离验收通过；服务器 Web 集成尚未完成。**
- 上游依据：[能力图](CAPABILITY_MAP.md)、[目标架构](../TARGET_ARCHITECTURE.md)
- 后续模块：`web-frontend-integration`，本规格不提前实现 Web 嵌入或远端部署。

## 1. Objective

从现有 `infra/web` 中分离节点预览应用，在不运行聚合 Web 的本机，独立部署 Frontend 并展示 KT2 脉冲压缩、KT3 距离-多普勒链路的真实结果。

面向算法开发者：一条 Compose 命令启动案例后，浏览器直接看到输入/输出预览、节点状态和丢弃统计，不需要全局控制台。服务器部署由 Web 内嵌同一 Frontend 镜像和页面，不另做一套解码或绘图实现；本模块完成只是第一阶段。

### 必须保留的约束

- 功能源码与测试实现位于 `infra/web/`、`infra/frontend/`；KT2/KT3 各合并为根目录一份 `compose.yaml` 并删除旧 infra/worker 配置。能力图、规格、`infra/tasks/` 及案例 README 部署说明为文档例外。
- 不增加叠加配置文件，不维护旧部署兼容层。KT2/KT3 的算法源码、测试、数据、CMake、Dockerfile 和结果契约保持不变。
- Sidecar、SDK、Ring、协议文件及公共发布脚本只读。复用现有遥测/预览接口；发现必须越界时先暂停说明，不自动扩大范围。
- 以本任务开始时的工作树快照为基线检查改动；仓库此前的迁移、重命名等未提交修改不归本任务，也不得覆盖。
- 本期只迁移已有能力，不新增录制、算法或 SDK 输出，不把 mock、静态图片或历史文件回放冒充真实在线预览。
- **所有目标镜像及构建、测试、运行容器统一使用 linux/arm64**。本机为 x86_64，也运行 ARM64 Docker；不得用 AMD64 镜像完成本机验收后再换另一套 ARM64 镜像上服务器。
- 每节点 Frontend 是独立容器，包含页面和必要轻量后端。多个节点复用镜像，通过配置和镜像版本区分，不复制每节点代码。

### 分阶段交付

1. 本模块：本机 TCP、无 Web，KT2 和 KT3 分别跑通独立 Frontend。
2. 下一模块：同一 Frontend 嵌入 `192.162.2.64` 上的 Web，由该 Web 通过 SSH/Compose 部署选定服务器上的 Docker 链路并验证内嵌结果，这是本期多机验收。Web 是多机部署控制台，不是所有节点的唯一运行位置；只跳转到外部页面不满足嵌入要求。
部署说明、镜像记录与 Harbor 拉取验证随两阶段交付，不另设第三个部署配置模块。

## 2. Tech Stack

优先沿用当前实现，不升级依赖或引入微前端框架：

| 部分 | 当前基线 |
|---|---|
| HTTP、遥测、预览接收 | Go，现有 `go.mod` 要求 1.24.0 |
| 浏览器渲染 | React 18、TypeScript 5、Vite 6、Canvas；精确依赖以现有 lockfile 为准 |
| 单元测试 | Go testing、Vitest 3 |
| 传输格式 | 现有 `proto/telemetry.proto`、`proto/preview.fbs`，预览协议版本 1 |
| WebSocket | 现有 gorilla/websocket 1.5.3 |
| 运行 | Docker Compose；案例数据链使用 UCX functional / tcp,self |
| 镜像 | 统一 linux/arm64；Harbor `registry.chengyistudio.com/cxx`，部署固定 manifest digest |

当前本机及 Docker 引擎为 **x86_64**。已依用户授权注册 qemu-aarch64，ARM64 基座容器实际返回 `aarch64` / `arm64`，原 `exec format error` 阻塞解除。见 [环境证据](tasks/evidence/arm64-environment.md)。一次性注册工具使用宿主架构，业务构建/测试/运行仍全部 ARM64；注册未设置开机持久化，宿主重启后须重新检查。

发布沿用已核实的现有流程：

- [Web build-base](web/docker/README.md)：x86 构建机用 `docker buildx build --platform linux/arm64` 发布 ARM64 基座，缓存 Go/Protobuf/Node 与对应 lockfile 的依赖；应用镜像在 ARM64 发布环境构建并验证。
- [Sidecar 双基座](sidecar/docker/README.md)：`build-base`、`runtime-base` 的 Dockerfile 均固定 `linux/arm64`，业务镜像复用基座，不在验收部署时现场安装编译依赖。
- 正式应用发布使用 `.agents/skills/docker-release/scripts/release.sh`，在 ARM64 发布环境验证镜像契约后推送 Harbor；`verify-image-contract.sh` 强制校验 ARM64。
- 公共发布脚本目前不支持 Frontend，保持不改。Frontend 复用现有 ARM64 基座，用 Dockerfile 测试阶段与原生 Docker 命令发布，操作记录在 `infra/frontend/README.md`；保留源码版本、不可变标签、架构/入口/digest 检查和推送拉回要求，不再复制发布器与契约校验工具链。

## 3. Project Structure

独立应用布局：

```text
workspace/infra/
├── frontend/
│   ├── cmd/frontend/          # 独立进程入口，不启动编排服务
│   ├── internal/             # 节点身份、遥测、预览与 HTTP/WS
│   ├── ui/                   # 节点页面、波形与 RD 图；测试与源码相邻
│   ├── web/                  # 构建后的静态资源与 Go embed
│   ├── go.mod
│   ├── Dockerfile            # ARM64 构建、测试阶段与运行镜像
│   └── README.md             # 部署说明与受控发布操作
├── web/                      # 保留现有聚合管理应用及其前端
├── proto/                    # 复用现有协议，不新增平行协议
└── tasks/
    ├── README.md
    └── evidence/             # 后续脱敏验收证据，不存密钥或大型原始数据
```

复用现有 `PreviewPanel.tsx`、波形/RD 解码与渲染、Go preview 接收实现及测试。共享或迁移的具体源码位置在 Plan 阶段确定，不为了共用少量代码预先建立通用插件框架。

案例部署入口为 `workspace/examples/KT2/compose.yaml`、`workspace/examples/KT3/compose.yaml`，替代原四份 infra/worker Compose，并同步案例 README。功能验证代码放 `infra/frontend/`，验收证据放 `infra/tasks/evidence/`；范围检查使用 Git 与 sha256sum，不增加自定义检查工具。

## 4. Interface Requirements

### 节点绑定与协议

- 每个 Frontend 实例显式绑定一个节点 ID；不同实例的 HTTP、遥测、预览监听地址不能冲突。
- 接收现有 Sidecar UDP 遥测和 TCP 预览，浏览器使用 HTTP/WebSocket；Frontend 不读取主数据 Ring，不加入 Worker IPC，不挂载 Docker Socket。
- 使用现有 `NODE_ID`、`TELEMETRY_HOST`/`TELEMETRY_PORT`、`PREVIEW_HOST`/`PREVIEW_PORT` 配置连接 Sidecar；Frontend 监听配置优先沿用 `TELEMETRY_UDP_ADDR`、`PREVIEW_TCP_ADDR`、`TELEMETRY_HTTP_ADDR`。
- 从现有 SidecarHello 的 stream descriptors 获取可用 Leg 与类型/版本，不依赖 Web 的镜像发现或编排接口才能显示预览。对外节点信息须能提供这些描述。
- 拒绝节点身份不匹配、未知协议版本、畸形/超长消息和越权订阅；节点 ID 是路由信息，不是鉴权凭据。
- 沿用二进制预览语义与现有 8 MiB 单消息上限，不为了拆分修改 Frame ABI、数据类型或精度。

### 页面与后端

- Web 页面端口保留 `8080/TCP`；Frontend 页面与浏览器 WebSocket 共用 `8081/TCP`。同机多个 Frontend 依次使用 8082、8083、8084；不同服务器可各自使用 8081。端口均可配置，启动前检查占用。
- 遥测 UDP 和 Sidecar 预览 TCP 不使用页面端口，按实例单独配置，端口表见 `tasks/plan.md`。多机 Web 的 `9900/UDP` 仍直接接收 Sidecar 全局遥测；Web 通过 Frontend 的 HTTP/WebSocket 端口（默认 8081）代理节点页面，不通过 9900/9901 连接 Frontend。旧 Web `9901/TCP` 预览监听在 G01 切换时删除，不保留兼容入口；单机无 Web 时遥测与预览均直送对应 Frontend。
- 展示既有输入/输出波形、RD 热力图、实际 FPS、snapshot/encode/network drops，以及链路/Ring 状态；不存在的 Leg 不展示为故障。
- 明确区分连接中、断开、暂无数据、类型不支持等状态；重连或节点实例变更后不得继续把旧图标为实时结果。
- HTTP 根路径提供节点页面；状态接口和 `/ws/frames` 保持可复用语义。提供健康检查，并区分“进程就绪”与“收到真实数据”。
- 为后续嵌入保留子路径部署能力：静态资源、HTTP 请求和 WebSocket URL 不硬编码到聚合 Web 的根路径；独立根路径访问和代理子路径访问使用同一应用。
- Web 的部署与全局管理能力必须保留；节点预览迁入 Frontend，切换内嵌入口时删除旧预览渲染和转发路径，不新增旧入口兼容、双版本适配或回退机制。

### 隔离与安全

- 沿用按需订阅、有界缓存和拥塞丢弃机制，保留当前双 Leg 合计最高 30 fps 的请求预算，不要求模拟环境达到该帧率。
- Frontend 停止、重启、浏览器关闭或慢读时，Worker/Sidecar 不得被连带停止，主数据计数应继续增长。
- 独立本机预览默认监听 loopback，不新增账号或登录系统。服务器 Frontend 的管理网端口仅允许 Web 服务器访问，通过 Web 内嵌提供页面；沿用现有 Web 会话、Cookie/CSRF、TLS 和同源检查，不为嵌入放宽现有保护。Frontend 无需再向用户索要服务器密码。
- Frontend 不携带集群 SSH 凭据，不提供部署/启停容器接口。SSH 密码/私钥继续由 Web 后端现有会话机制管理，不传给 Frontend 或 iframe；密钥不进入镜像、YAML、URL 查询参数或验收日志。

## 5. Docker 与 Harbor 部署契约

### 配置限制

- KT2、KT3 各在根目录提供一份 `compose.yaml`、一个 project，包含完整数据链与各节点 Frontend；删除旧 infra/worker Compose，不维护平行入口或启动包装脚本。
- 默认镜像为已验证的 ARM64 Harbor digest，节点身份、端口、UCX `functional / tcp,self` 直接配置；不要求必填环境变量或额外 `.env`，必要参数仍可显式调整。
- Worker 使用 `ipc: service:对应Sidecar`，Sidecar 保持 shareable IPC 与原 SHM 参数；由 `depends_on: condition: service_healthy` 表达依赖，不靠固定外部容器名或人工启动顺序。保留健康检查、帧契约和结果校验，Frontend 不作为数据链依赖。
- 日常启动只有 `docker compose up -d --no-build`，缺镜像自动从 Harbor 拉取，失败报错；Compose 不含 build 或本地 dev 回退，需要构建时走已有 Dockerfile 和受控发布流程。
- 默认不运行 Web 或 Nginx；KT2、KT3 可依次验收，本期不要求两套案例同时占用同一组端口。
- 接入时不能仅把所有 Sidecar 留在同一个默认预览端口，而声称实现了每节点独立 Frontend。

### 构建与部署分离

- **ARM 服务器构建/测试 → 发布 Harbor → 本机拉取固定 digest → 无构建启动**。所有应用测试与运行容器均为 ARM64。
- 当前两个 infra Compose 已大量使用 Harbor digest；原 worker Compose 的 `build:` 与本地 `:dev` 路径随合并移除，不维持旧启动方式。
- 新 Compose 直接内置经验证的镜像 digest；若保留 `FRONTEND_IMAGE` 等调整参数，也必须提供可直接运行的默认值，不要求部署者另填镜像清单。正式引用不只依赖 latest 或本地镜像 ID。
- 没有已发布镜像时，记录缺失项并走受控发布；仓库或认证不可用时报告阻塞。不得偷偷改为部署机现场 `docker build`、源码挂载、`docker save/load` 来获得通过结论。
- KT3 所需的真实 `algorithm/GFKD_V1_ARM` **已在本地**，为 394352 字节的 ARM aarch64 可执行文件，权限 755，四份 CSV 也在。Dockerfile 已通过 `COPY algorithm/ /app/algorithm/` 包含编译产物，不需要 GFKD 源码；被 Git 忽略不表示未提供。
- 本地已有 ARM64 `registry.chengyistudio.com/cxx/worker:rd-algorithm-v1.0.0`，缓存中的仓库 digest 为 `sha256:0213dfc739c7ca3ca6f689e03ac02a2a2ad29f6ac74a85a5b38f77bf1d89e00a`，作为后续拉取复验候选，不宣称本轮已经拉取或验收。
- 对镜像记录仓库引用、manifest digest、实际架构、源码版本与拉取结果。使用原生 Docker 命令与受控发布记录，不修改公共 release 脚本，也不绕过既有发布约束；只有测试通过、源码明确且标签确认为未占用时才允许推送。

## 6. Commands

除明确进入案例目录的命令外，均从仓库根目录执行。环境检查与构建测试供开发/验收使用，不是日常部署步骤；Frontend、正式镜像和两个单一 Compose 已交付，实际结果以 evidence 为准。

### 环境检查（开发/验收）

```bash
uname -m
docker info --format 'Docker architecture={{.Architecture}}'
docker compose version
```

### Frontend ARM64 构建与检查

在 ARM 构建机执行；Dockerfile test 阶段复用已有 Go/Vitest 测试与 UI 构建，不另建测试包装脚本：

```bash
test "$(uname -m)" = aarch64
docker build --target test \
  -f workspace/infra/frontend/Dockerfile -t uestcradar/frontend:test .
```

复用现有 Web build-base 和 lockfile 缓存，不预先更新依赖或基座；确有缺失再说明并修正。最终镜像需用户批准后发布 Harbor，本机拉取同一 digest 验证，不把测试镜像当成部署发布。

### 日常单机部署（目标 Compose 与镜像发布完成后）

一次性前提为 Docker 可运行 ARM64、Harbor 可访问并已按需登录。默认镜像 digest、端口与节点配置随 Compose 提供，无需额外环境文件，也不需要运行测试或检查脚本：

```bash
cd workspace/examples/KT2  # 或 workspace/examples/KT3
docker compose up -d --no-build
```

浏览器直接访问 KT2 算法页 `http://127.0.0.1:8082` 或 KT3 RD 页 `http://127.0.0.1:8083`。在同一案例目录用 `docker compose ps` 查看状态，`docker compose down` 停止本次案例。两案例默认顺序运行，已有同名 project 或占用端口时先确认，不自动替换其他工作负载。

配置解析、显式 Harbor pull、镜像/IPC 检查与结果取证归 [plan 第 7 节](tasks/plan.md)，不作为日常部署步骤。服务名和端口表也在该计划中；只停止本次创建的 project 或指定 Frontend 服务。

## 7. Code Style

沿用现有 Go 与 TypeScript 风格，不做无关格式化。Go 使用 gofmt、显式错误返回；TypeScript 使用现有严格类型和测试约定。下面来自当前预览后端的读写风格，强调处理短写和错误，不增加通用封装：

```go
func writeFull(writer io.Writer, data []byte) error {
    for len(data) > 0 {
        written, err := writer.Write(data)
        if err != nil {
            return err
        }
        if written == 0 {
            return io.ErrShortWrite
        }
        data = data[written:]
    }
    return nil
}
```

保留已有波形、RD 轴与池化解释，不通过重绘“看起来相似”的静态图替代解码测试。

## 8. Testing Strategy

- **单元与接口**：迁移/复用 Go testing、Vitest 覆盖节点过滤、stream descriptors、类型版本、波形/RD 解码、消息上限、无效订阅、有界队列、断线重连与子路径 URL。既有测试不能因迁移而直接删除。
- **Compose 静态验证**：两份新 compose.yaml 在无 .env/额外业务变量时可解析，默认镜像固定 digest，案例各只有一个 project；检查 service IPC、启动依赖、节点 ID、观测目标与端口，旧四份配置已删除，不泄露凭据。
- **真实链路验证**：本机实际运行 KT2、KT3，保留既有 Worker/Sink 的校验结果，同时确认浏览器真实渲染并持续更新。容器 healthy、HTTP 200、WebSocket 连接成功或单张截图均不足以证明通过。
- **隔离验证**：关闭页面、停止并重启目标 Frontend、模拟慢预览消费者，比较 Worker/Sink 数据计数；主链不能因旁路故障停止。
- **部署验证**：构建、测试和运行均使用 ARM64 Docker。正式复验执行 Harbor pull 和 `up --no-build`，记录镜像 digest、ARM64 架构、传输模式、页面证据和时间。不能以 AMD64 开发镜像代替，或把本地开发镜像测试冒充发布镜像测试。
- **改动范围验证**：相对任务起点快照确认案例仅合并 Compose、删除四份旧配置并同步 README 部署说明；算法/数据、Sidecar、SDK、协议和公共发布脚本未改，功能实现位于 web/frontend。

## 9. Boundaries

- **Always**：先确认规格与计划；保留现有接口校验与有界预览；审查 Docker 配置、运行相关测试并保存真实证据；遵守 Harbor 发布与部署隔离；缺少镜像、依赖或硬件时报告阻塞。
- **Ask first**：改变 SDK/Frame/预览协议、修改 web/frontend 与本次案例 Compose 合并范围之外的代码或配置、新增依赖或 CI、注册特权 ARM64 模拟、改变宿主机网络/内核/RDMA 配置、跳过 Harbor 使用临时镜像、影响目标机现有容器。
- **Never**：新增录制功能；在 Frontend 中嵌入编排权限或 SSH 密钥；让 Frontend 成为第二个 Ring 消费者；通过弱化算法测试、mock 结果或静默 TCP 降级通过验收；擅自提交或推送工作区修改。

## 10. Success Criteria

| 编号 | 可验证完成条件 |
|---|---|
| F01 | KT2/KT3 各在案例根目录凭一份 compose.yaml、一个 project，以单次 up -d --no-build 启动完整链路和 Frontend；默认无 Web/Nginx，无额外环境文件或人工 infra/worker 顺序，旧配置已删除；仅同步部署说明，不改案例算法、测试和数据 |
| F02 | KT2 算法节点显示 IQ 输入 `1:3` 与脉压输出 `2:2`；KT3 算法节点显示脉压输入 `2:2` 与 RD 输出 `3:2`，来自该次真实运行且既有结果校验通过 |
| F03 | 每个案例至少连续观察 60 秒，输入/输出各取得不少于两个不同 frame_id 的有效预览，页面实际绘制；不要求模拟环境达到 30 fps |
| F04 | 节点身份、Leg 与类型正确；一个实例不显示另一节点数据；错误版本和畸形消息被拒绝，断线状态不冒充实时画面 |
| F05 | Frontend 停止 30 秒期间主链处理计数继续增长；恢复后 30 秒内重新得到有效预览，Worker/Sidecar 不因测试被重启 |
| F06 | 根路径与预定代理子路径下静态资源及 WebSocket 可用；保留现有 TLS/同源与 Web 会话保护，Frontend 无部署接口、SSH 凭据和 Docker Socket，不要求新增登录系统 |
| F07 | 全程使用 ARM64 Docker；至少一次从 Harbor 拉取固定 digest 后，以 `--no-build` 完成两个案例本机部署复验；记录镜像引用、ARM64 架构、transport 和脱敏证据 |
| F08 | Frontend 相关 Go/Vitest 测试及构建通过；KT2/KT3 修改范围检查通过；未执行项和环境阻塞单独列出 |

这些门槛用于功能验收，不代表吞吐、零丢失、原生 ARM64 性能或真实 RDMA 性能结论。

## 11. Open Questions / 前置条件

1. 本机 ARM64 环境与两个真实案例已验收；宿主重启后仍须重新验证 binfmt，不代表原生性能保证。
2. Frontend 已发布并拉回；KT3 使用真实 GFKD，与受保护本地产物哈希相符。旧 Sink 镜像的 65 列限制已用原有动态尺寸校验器重新构建发布解决，没有修改算法或测试。
3. **权限要求并不意味着新建认证系统**：Web 继续使用原来的 SSH 会话与密码/私钥处理；Frontend 只显示节点结果，不能接收这些凭据。嵌入方式由后续技术计划落地，保留现有安全检查即可。
4. **隔离要求的具体含义**：关闭/重启 Frontend 时，算法链继续传输，Web 仍能查看节点并执行既有部署管理；最多是该节点预览暂不可用。实现时需要正确连接现有遥测接口，不要求用户再设计一套观测系统。
5. **多机验收入口已确定**：把更新后的 Web 部署到 `192.162.2.64`，通过它部署选定服务器的 Docker 链路，验证内嵌 Frontend 展示。记录 Web 地址、受管节点、镜像及实际 transport；本期不额外增加 RDMA 性能压测。该验收在本机 KT2/KT3 验收之后进行。

**执行状态以 [todo.md](tasks/todo.md) 与 evidence 为准。应用构建/单元测试通过不等于真实案例通过；发布、源码提交与已有工作负载变更仍遵守已定操作门槛。**
