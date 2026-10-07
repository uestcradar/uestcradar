# Spec: 独立节点 Frontend 与 KT2/KT3 本机验收

- Module id：`frontend-runtime`
- 状态：**按用户要求作为 Plan/Tasks 的范围基线；计划与 todo 已生成待审，尚未开始实现。**
- 上游依据：[能力图](CAPABILITY_MAP.md)、[目标架构](../TARGET_ARCHITECTURE.md)
- 后续模块：`web-frontend-integration`，本规格不提前实现 Web 嵌入或远端部署。

## 1. Objective

从现有 `infra/web` 中分离节点预览应用，在不运行聚合 Web 的本机，独立部署 Frontend 并展示 KT2 脉冲压缩、KT3 距离-多普勒链路的真实结果。

面向算法开发者：启动既有 Worker 和基础设施后，用浏览器直接访问节点页面，看到输入/输出预览、节点状态和预览丢弃统计，不需要启动全局编排控制台。

### 必须保留的约束

- 功能源码、测试和部署配置的修改范围**仅限 `infra/web/`、`infra/frontend/`**；能力图、规格和 `infra/tasks/` 为文档例外。
- KT2/KT3 **所有原文件只读，包括原 Compose 与 Dockerfile**。通过 `infra/frontend/docker/` 下的 Compose override 接入预览、覆盖镜像引用和观测端口。
- Sidecar、SDK、Ring、协议文件及公共发布脚本只读。复用现有遥测/预览接口；发现必须越界时先暂停说明，不自动扩大范围。
- 以本任务开始时的工作树快照为基线检查改动；仓库此前的迁移、重命名等未提交修改不归本任务，也不得覆盖。
- 本期只迁移已有能力，不新增录制、算法或 SDK 输出，不把 mock、静态图片或历史文件回放冒充真实在线预览。
- **所有目标镜像及构建、测试、运行容器统一使用 linux/arm64**。本机为 x86_64，也运行 ARM64 Docker；不得用 AMD64 镜像完成本机验收后再换另一套 ARM64 镜像上服务器。
- 每节点 Frontend 是独立容器，包含页面和必要轻量后端。多个节点复用镜像，通过配置和镜像版本区分，不复制每节点代码。

### 分阶段交付

1. 本模块：本机 TCP、无 Web，KT2 和 KT3 分别跑通独立 Frontend。
2. 下一模块：同一 Frontend 嵌入 `192.162.2.64` 上的 Web，由该 Web 通过 SSH/Compose 部署选定服务器上的 Docker 链路并验证内嵌结果，这是本期多机验收。Web 是多机部署控制台，不是所有节点的唯一运行位置；只跳转到外部页面不满足嵌入要求。
3. 最后整理统一部署配置与发布清单；Harbor 拉取要求贯穿前两阶段，不等到最后才验证。

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
- Frontend 复用这套基座、构建与发布方式，但不修改公共发布脚本；独立发布入口与契约检查放 `infra/frontend/docker/`，遵守同等 ARM64、源码版本、不可变标签、推送拉回验证要求。

## 3. Project Structure

以下为拟定布局，目录尚未实现：

```text
workspace/infra/
├── frontend/
│   ├── cmd/frontend/          # 独立进程入口，不启动编排服务
│   ├── internal/             # 节点身份、遥测、预览与 HTTP/WS
│   ├── ui/                   # 节点页面、波形与 RD 图；测试与源码相邻
│   ├── web/                  # 构建后的静态资源与 Go embed
│   ├── go.mod
│   ├── Dockerfile
│   ├── docker/               # 发布入口、ARM64 检查与 KT2/KT3 外置 override
│   ├── tests/                # 修改范围检查与应用验证代码
│   └── README.md
├── web/                      # 保留现有聚合管理应用及其前端
├── proto/                    # 复用现有协议，不新增平行协议
└── tasks/
    ├── README.md
    └── evidence/             # 后续脱敏验收证据，不存密钥或大型原始数据
```

复用现有 `PreviewPanel.tsx`、波形/RD 解码与渲染、Go preview 接收实现及测试。共享或迁移的具体源码位置在 Plan 阶段确定，不为了共用少量代码预先建立通用插件框架。

KT2/KT3 不做任何直接修改；功能验证脚本放 `infra/frontend/`，验收证据放 `infra/tasks/evidence/`，不改两个案例的测试程序或原 YAML。

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
- 遥测 UDP 和 Sidecar 预览 TCP 不使用页面端口，按实例单独配置；具体端口表见 `tasks/plan.md`，保留旧 Web 的 9900/9901 端口。
- 展示既有输入/输出波形、RD 热力图、实际 FPS、snapshot/encode/network drops，以及链路/Ring 状态；不存在的 Leg 不展示为故障。
- 明确区分连接中、断开、暂无数据、类型不支持等状态；重连或节点实例变更后不得继续把旧图标为实时结果。
- HTTP 根路径提供节点页面；状态接口和 `/ws/frames` 保持可复用语义。提供健康检查，并区分“进程就绪”与“收到真实数据”。
- 为后续嵌入保留子路径部署能力：静态资源、HTTP 请求和 WebSocket URL 不硬编码到聚合 Web 的根路径；独立根路径访问和代理子路径访问使用同一应用。
- 本模块不移除现有 Web 正常管理能力；重复的 Web 节点预览渲染路径在后续集成模块统一替换，避免提前破坏旧入口。

### 隔离与安全

- 沿用按需订阅、有界缓存和拥塞丢弃机制，保留当前双 Leg 合计最高 30 fps 的请求预算，不要求模拟环境达到该帧率。
- Frontend 停止、重启、浏览器关闭或慢读时，Worker/Sidecar 不得被连带停止，主数据计数应继续增长。
- 独立本机预览默认监听 loopback，不新增账号或登录系统。服务器 Frontend 的管理网端口仅允许 Web 服务器访问，通过 Web 内嵌提供页面；沿用现有 Web 会话、Cookie/CSRF、TLS 和同源检查，不为嵌入放宽现有保护。Frontend 无需再向用户索要服务器密码。
- Frontend 不携带集群 SSH 凭据，不提供部署/启停容器接口。SSH 密码/私钥继续由 Web 后端现有会话机制管理，不传给 Frontend 或 iframe；密钥不进入镜像、YAML、URL 查询参数或验收日志。

## 5. Docker 与 Harbor 部署契约

### 配置限制

- 保留 KT2/KT3 的 Worker/infra 分离部署模式和原有健康检查、IPC、帧契约、结果校验；原文件不改。
- 在 `infra/frontend/docker/` 下建立四份外置 override，每次先加载原 Compose、再加载对应 override。通过覆盖层新增 Frontend、设置身份与观测端口，不合并原有两个 project。
- 不使用要求升级现有 Compose 才支持的特殊 YAML reset 标签；即使原 worker 配置保留 build 字段，也必须以 `--no-build` 启动。
- 默认不运行 Web 或 Nginx；KT2、KT3 可依次验收，本期不要求两套案例同时占用同一组端口。
- 接入时不能仅把所有 Sidecar 留在同一个默认预览端口，而声称实现了每节点独立 Frontend。

### 构建与部署分离

- 本地构建可用于开发检查；**正式复验优先走构建测试 → 发布 Harbor → 拉取固定 digest → 无构建启动**。
- 两个 infra Compose 已大量使用 Harbor digest；两个 worker Compose 仍配置 `build:` 与本地 `:dev` 标签，必须区分开发构建入口和拉取部署入口。
- 拟通过 `FRONTEND_IMAGE`、`KT2_WORKER_IMAGE`、`KT3_WORKER_IMAGE` 引用已发布镜像，并保留现有 Sidecar/Source/Sink 镜像参数；正式配置都固定 digest，不仅依赖 `latest` 或本地镜像 ID。
- 没有已发布镜像时，记录缺失项并走受控发布；仓库或认证不可用时报告阻塞。不得偷偷改为部署机现场 `docker build`、源码挂载、`docker save/load` 来获得通过结论。
- KT3 所需的真实 `algorithm/GFKD_V1_ARM` **已在本地**，为 394352 字节的 ARM aarch64 可执行文件，权限 755，四份 CSV 也在。Dockerfile 已通过 `COPY algorithm/ /app/algorithm/` 包含编译产物，不需要 GFKD 源码；被 Git 忽略不表示未提供。
- 本地已有 ARM64 `registry.chengyistudio.com/cxx/worker:rd-algorithm-v1.0.0`，缓存中的仓库 digest 为 `sha256:0213dfc739c7ca3ca6f689e03ac02a2a2ad29f6ac74a85a5b38f77bf1d89e00a`，作为后续拉取复验候选，不宣称本轮已经拉取或验收。
- 对镜像记录仓库引用、manifest digest、实际架构、源码版本与拉取结果。新 Frontend 使用自己目录内的发布与契约检查入口，不修改公共 release 脚本，也不绕过既有发布约束。

## 6. Commands

以下均从仓库根目录执行。现有配置检查现在可用；Frontend 构建命令与拉取部署命令是待实现后的验收入口，**本阶段未执行，不表示当前已可运行**。

### 环境与当前 Compose 检查

```bash
uname -m
docker info --format 'Docker architecture={{.Architecture}}'
docker compose version
docker compose -f workspace/examples/KT2/docker-compose-infra.yaml config --quiet
docker compose -f workspace/examples/KT2/docker-compose-worker.yaml config --quiet
docker compose -f workspace/examples/KT3/docker/docker-compose-infra.yaml config --quiet
docker compose -f workspace/examples/KT3/docker/docker-compose-worker.yaml config --quiet
```

### Frontend ARM64 容器构建与检查（拟定）

`FRONTEND_BUILD_BASE` 指向已发布的 ARM64 构建基座 digest，复用 Web 的依赖缓存方式。x86 本机需先具备 ARM64 模拟运行能力；不在宿主机直接运行 Go/Node 测试来替代 ARM64 容器验收。

```bash
: "${FRONTEND_BUILD_BASE:?请设置已发布的 ARM64 构建基座 digest}"
docker pull --platform linux/arm64 "$FRONTEND_BUILD_BASE"
docker image inspect --format '{{.Os}}/{{.Architecture}}' "$FRONTEND_BUILD_BASE"
docker run --rm --platform linux/arm64 \
  --user "$(id -u):$(id -g)" \
  --mount type=bind,src="$PWD",dst=/src \
  -w /src/workspace/infra/frontend \
  -e HOME=/tmp -e GOCACHE=/tmp/go-build -e GOPATH=/tmp/go \
  "$FRONTEND_BUILD_BASE" sh -ec '
    cmp ui/package-lock.json /opt/web-frontend/package-lock.json
    test ! -e ui/node_modules || { echo "use a clean build workspace" >&2; exit 1; }
    cp -a /opt/web-frontend/node_modules ui/node_modules
    npm --prefix ui test -- --run
    npm --prefix ui run build
    go test ./...
    go vet ./...
    go build -trimpath -o /tmp/uestcradar-frontend ./cmd/frontend
  '
git diff --check
```

在干净构建工作区执行。lockfile 与缓存不匹配时先更新 ARM64 基座，不在部署时补装依赖。现有包没有独立 lint script，不虚构 `npm run lint`；TypeScript 检查包含在 build 中，Go 使用 gofmt。最终镜像仍需通过受控发布流程推送 Harbor，再由验收端拉取。

### 本机拉取部署（目标 Compose 完成后）

`DEPLOY_ENV` 指向经过审阅的本地 Docker 环境文件，所有镜像均为 linux/arm64 并固定 digest，包含节点端口配置，不含提交到 Git 的密钥。镜像发布完成前不要执行。保留分开的 Compose project，以匹配现有 Worker 的 IPC 连接方式。

```bash
: "${DEPLOY_ENV:?请设置已审阅的 Docker 环境文件绝对路径}"
CASE=kt2
BASE=workspace/examples/KT2
OVER=workspace/infra/frontend/docker
for PART in infra worker; do
  docker compose --env-file "$DEPLOY_ENV" \
    -f "$BASE/docker-compose-$PART.yaml" -f "$OVER/$CASE-$PART.override.yaml" config --quiet
  docker compose --env-file "$DEPLOY_ENV" \
    -f "$BASE/docker-compose-$PART.yaml" -f "$OVER/$CASE-$PART.override.yaml" pull
  docker compose --env-file "$DEPLOY_ENV" \
    -f "$BASE/docker-compose-$PART.yaml" -f "$OVER/$CASE-$PART.override.yaml" up -d --no-build
done
```

KT2 验收并停止其测试容器后，切换对应环境文件，设置 `CASE=kt3`、`BASE=workspace/examples/KT3/docker`，执行同一段。两个案例的原 YAML 都只作为只读输入。

Frontend 服务名和端口表见 `tasks/plan.md`；按原 Compose + 对应 override 操作具体服务，停止命令不得省略覆盖配置。只清理本次创建的容器，不清理宿主机其他工作负载。

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
- **Compose 静态验证**：四份 Compose 均可解析；检查所有镜像、IPC、节点 ID、观测目标地址与监听端口，不泄露环境文件中的凭据。
- **真实链路验证**：本机实际运行 KT2、KT3，保留既有 Worker/Sink 的校验结果，同时确认浏览器真实渲染并持续更新。容器 healthy、HTTP 200、WebSocket 连接成功或单张截图均不足以证明通过。
- **隔离验证**：关闭页面、停止并重启目标 Frontend、模拟慢预览消费者，比较 Worker/Sink 数据计数；主链不能因旁路故障停止。
- **部署验证**：构建、测试和运行均使用 ARM64 Docker。正式复验执行 Harbor pull 和 `up --no-build`，记录镜像 digest、ARM64 架构、传输模式、页面证据和时间。不能以 AMD64 开发镜像代替，或把本地开发镜像测试冒充发布镜像测试。
- **改动范围验证**：相对任务起点快照确认 KT2/KT3 全部原文件（含 Docker 配置）、Sidecar、SDK、协议和公共发布脚本未改；功能变更仅在 web/frontend 两个允许目录内，文档按例外处理。

## 9. Boundaries

- **Always**：先确认规格与计划；保留现有接口校验与有界预览；审查 Docker 配置、运行相关测试并保存真实证据；遵守 Harbor 发布与部署隔离；缺少镜像、依赖或硬件时报告阻塞。
- **Ask first**：改变 SDK/Frame/预览协议、修改两个允许功能目录以外的代码或配置、新增依赖或 CI、注册特权 ARM64 模拟、改变宿主机网络/内核/RDMA 配置、跳过 Harbor 使用临时镜像、影响目标机现有容器。
- **Never**：新增录制功能；在 Frontend 中嵌入编排权限或 SSH 密钥；让 Frontend 成为第二个 Ring 消费者；通过弱化算法测试、mock 结果或静默 TCP 降级通过验收；擅自提交或推送工作区修改。

## 10. Success Criteria

| 编号 | 可验证完成条件 |
|---|---|
| F01 | 在本机无 Web/Nginx 容器运行时，KT2 与 KT3 各自通过原 Compose + Frontend 目录内 override 启动独立 Frontend，案例所有原文件保持不变 |
| F02 | KT2 算法节点显示 IQ 输入 `1:3` 与脉压输出 `2:2`；KT3 算法节点显示脉压输入 `2:2` 与 RD 输出 `3:2`，来自该次真实运行且既有结果校验通过 |
| F03 | 每个案例至少连续观察 60 秒，输入/输出各取得不少于两个不同 frame_id 的有效预览，页面实际绘制；不要求模拟环境达到 30 fps |
| F04 | 节点身份、Leg 与类型正确；一个实例不显示另一节点数据；错误版本和畸形消息被拒绝，断线状态不冒充实时画面 |
| F05 | Frontend 停止 30 秒期间主链处理计数继续增长；恢复后 30 秒内重新得到有效预览，Worker/Sidecar 不因测试被重启 |
| F06 | 根路径与预定代理子路径下静态资源及 WebSocket 可用；保留现有 TLS/同源与 Web 会话保护，Frontend 无部署接口、SSH 凭据和 Docker Socket，不要求新增登录系统 |
| F07 | 全程使用 ARM64 Docker；至少一次从 Harbor 拉取固定 digest 后，以 `--no-build` 完成两个案例本机部署复验；记录镜像引用、ARM64 架构、transport 和脱敏证据 |
| F08 | Frontend 相关 Go/Vitest 测试及构建通过；KT2/KT3 修改范围检查通过；未执行项和环境阻塞单独列出 |

这些门槛用于功能验收，不代表吞吐、零丢失、原生 ARM64 性能或真实 RDMA 性能结论。

## 11. Open Questions / 前置条件

1. 本机 ARM64 运行环境已完成最小验证，F01 环境任务已关闭；真实 KT2/KT3 和 Frontend 尚未验收，宿主重启后须重新验证 binfmt。
2. KT3 ARM64 编译产物及已有 Worker 镜像已确认存在；新 Frontend 镜像需发布，其他镜像按既有 Harbor 引用拉取复验，不把 Git 忽略或尚未拉取误报为产物缺失。
3. **权限要求并不意味着新建认证系统**：Web 继续使用原来的 SSH 会话与密码/私钥处理；Frontend 只显示节点结果，不能接收这些凭据。嵌入方式由后续技术计划落地，保留现有安全检查即可。
4. **隔离要求的具体含义**：关闭/重启 Frontend 时，算法链继续传输，Web 仍能查看节点并执行既有部署管理；最多是该节点预览暂不可用。实现时需要正确连接现有遥测接口，不要求用户再设计一套观测系统。
5. **多机验收入口已确定**：把更新后的 Web 部署到 `192.162.2.64`，通过它部署选定服务器的 Docker 链路，验证内嵌 Frontend 展示。记录 Web 地址、受管节点、镜像及实际 transport；本期不额外增加 RDMA 性能压测。该验收在本机 KT2/KT3 验收之后进行。

**当前交付为用户要求的任务阶段产物：[plan.md](tasks/plan.md)、[todo.md](tasks/todo.md)。本轮未实施、未部署、未取得测试通过结论；开始执行前须确认任务清单及其中的特权环境准备、发布等操作门槛。**
