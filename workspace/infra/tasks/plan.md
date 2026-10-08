# Plan：同一 Frontend，单机直达、Web 内嵌

目标：从 Web 抽出已有节点预览，使本机直接访问和服务器经 Web 访问使用**同一 Frontend 镜像、页面、解码与绘图实现**。不改算法，不另做单机版 UI。

单机阶段已完成：独立 Frontend 经 ARM 构建测试、Harbor 发布与两个真实案例验收；Web 集成 HTTPS 计划已获实施确认；G03–G10 已完成，G11/G12 尚未完成，内网自动证书模式已实现，当前等待现有节点 SSH 访问条件。详细任务覆盖 `frontend-runtime`；随后完成 `web-frontend-integration` 才算整体目标交付。依据：[规格](../SPEC-frontend-runtime.md)、[能力图](../CAPABILITY_MAP.md)；执行：[todo.md](todo.md)。

## 1. 修改边界

- 功能实现只在 `infra/frontend/`、`infra/web/`；允许将 KT2/KT3 的四份旧 Compose 合并为案例根目录的两份 `compose.yaml`，删除旧配置并同步案例 README 部署说明。
- 案例算法、测试、数据、CMake、Dockerfile，以及 Sidecar、SDK、Ring、协议和公共发布脚本不改。能力图、规格与本任务目录可同步更新。
- 保留已有工作区修改；不自动提交、推送、发布或替换运行中的工作负载。必要的越界或宿主变更先确认。
- 不新增录制、账号、微前端框架、共享组件平台或兼容层；不为此次拆分预先重构基座与依赖。

## 2. 实施顺序

`F00–F02` 准备 → `F03–F11` 抽出应用 → `F12/F14` ARM 构建测试与发布 → `F15/F16` KT2 → `F17/F18` KT3 → `F21` 单机阶段收尾 → `G01` Web 内嵌与服务器验收。

先跑通两个本机案例，再改 Web 集成；不为未切换的旧入口增加兼容或回退逻辑。F21 不是整个拆分的完成标志。

## 3. 必要实现

### 抽出已有预览，不重写

- Frontend 为独立 Go 进程加现有 React/TypeScript 页面，使用现有依赖版本与静态资源嵌入方式。
- 迁入现有 preview 服务、波形/RD 解码和渲染及其测试；保留协议、精度、坐标轴、有界缓存和拥塞丢弃语义。生成类型直接复用，必要的 Go 包路径调整留在 Frontend 内，不新增生成工具链。
- 沿用遥测 store/hub、快照及 `/ws`；预览仍使用 `/ws/frames`。Frontend 仅接纳配置的 `NODE_ID`，不导入 SSH、Docker 编排或 Web 会话模块。
- 增加最小 `/api/node`，把 SidecarHello 中已有的节点、实例、连接状态和 streams 暴露给页面；现有遥测快照不含输入/输出类型，独立页面不能再依赖 Web 镜像编排信息。无需新建节点注册中心或额外状态层。
- 保留必要的 `/healthz` 和镜像内健康探测；就绪不等于收到数据。断开、无数据和过期帧必须明确显示。
- 资源/API/WebSocket 使用可支持代理前缀的相对地址。单机和内嵌不是两套页面，也不另造跨页面消息协议。

### Web 保留管理，节点页面交给 Frontend

- Web 保留拓扑、SSH/Compose 部署、主机检查、全局遥测与现有会话安全。
- G01 用同源代理经 HTTPS/WSS 直连节点，内嵌同一 Frontend；SSH 仅用于部署管理，不建立预览隧道；切换后删除 Web 旧节点预览渲染、预览转发实现与 `9901/TCP` 监听，不保留双实现或旧入口兼容。
- 单机 Frontend 监听 loopback；服务器采用用户确认的可信内网 HTTPS 模式，Web→Frontend 不校验证书，不要求 8081 白名单。浏览器沿用 Web 会话，SSH 密码/私钥不进入 Frontend、iframe 或日志，CSRF/Origin 与 SSH 主机信任保持。

## 4. 数据去向与端口

| 场景 | 浏览器入口 | Sidecar 遥测 | Sidecar 预览 |
|---|---|---|---|
| 单机 | 直接访问 Frontend | 对应 Frontend | 对应 Frontend |
| 服务器 | Web:8080 → Frontend HTTP/WS | 直接到 Web:9900/UDP | 对应 Frontend |

Web 通过 Frontend 的 HTTP/WebSocket 端口代理页面，不用 9900/9901 连接 Frontend。服务器嵌入页的遥测复用 Web 已有数据，具体路由在 G01 落实；不能让 Frontend 成为 Web 全局遥测的必经中继。

本机 host 网络默认值如下，端口仍可调整；两案例顺序运行，不改原 UCX 端口与 SHM 名称。

| 案例 | Frontend 服务 | NODE_ID | HTTP/WS TCP | 遥测 UDP | 预览 TCP |
|---|---|---|---|---|---|
| KT2 | frontend-source | local-source | 8081 | 9902 | 9903 |
| KT2 | frontend-operator | local-pulsecompression | 8082 | 9904 | 9905 |
| KT2 | frontend-sink | local-sink | 8083 | 9906 | 9907 |
| KT3 | frontend-source | local-iq-source | 8081 | 9902 | 9903 |
| KT3 | frontend-pulsecompression | local-pulsecompression | 8082 | 9904 | 9905 |
| KT3 | frontend-rd | local-qt5-rd-algorithm | 8083 | 9906 | 9907 |
| KT3 | frontend-sink | local-rd-sink | 8084 | 9908 | 9909 |

每台服务器只有一个逻辑节点时可使用 8081/9902/9903。9901 不再为 Web 预留。

## 5. 单机唯一部署入口

已交付 `workspace/examples/KT2/compose.yaml` 和 `workspace/examples/KT3/compose.yaml`。每案例一份配置、一个 project，包含完整数据链和各节点 Frontend，容器仍各自独立。

- 默认配置写好已验证的 ARM64 Harbor digest、节点、端口与 UCX `functional / tcp,self`；不启动 Web/Nginx。
- 不含 build、源码挂载或本地 dev 回退；不需要额外 `.env`、override、启动脚本或手动设置业务变量。缺镜像由 Compose 拉取，失败明确报错。
- Worker 用 `ipc: service:对应Sidecar`，Sidecar 保留 shareable IPC、SHM 参数与健康检查；通过 `depends_on` 的 `service_healthy` 保证启动顺序，不依赖外部固定容器名。
- Frontend 不加入 Worker IPC，Worker/Sidecar 不依赖 Frontend 的启动或健康状态。

Docker ARM64 环境及 Harbor 访问准备好后，从仓库根目录选择一个案例：

```bash
cd workspace/examples/KT2  # 或 workspace/examples/KT3
docker compose up -d --no-build
```

KT2 算法页：`http://127.0.0.1:8082`；KT3 RD 页：`http://127.0.0.1:8083`。同目录使用 `docker compose ps` 查看状态、`docker compose down` 停止本次案例。已有同名 project 或端口占用时先确认，不自动替换工作负载。

## 6. 构建与发布

固定流程：**ARM 服务器构建/测试 → Harbor → 本机拉取同一 digest**。所有应用构建、测试与运行容器均为 `linux/arm64`；本机 x86 使用已经验证的模拟环境，宿主重启后重新检查。

- 复用 Web ARM64 build-base、lockfile 与已有依赖；不预先改基座。若确有缺失，报告后只做必要修正。
- 在 Frontend Dockerfile 中安排测试阶段，使用原生 `docker build/push/pull/image inspect`。公共发布脚本目前不支持 Frontend，不强套 Web 身份，也不复制一套发布器、契约校验器或测试包装脚本。
- 发布操作在 `frontend/README.md` 记录：用户批准、干净且明确的源码版本、确认版本标签未占用、ARM64 测试通过后才推送，拉回检查架构、入口和 digest。不能把认证或网络失败当作标签不存在；不覆盖不可变版本，不要求新增滚动标签自动化。
- 不在部署端现场构建，不用 save/load 或源码挂载替代 Harbor。已有 Worker/Sidecar 继续使用已验证镜像与既有发布流程。

## 7. 开发与验收入口（不是部署步骤）

以下命令用于实现与验收，不能要求部署用户先执行。应用与案例实际结果见 evidence；不得仅凭文档中列有命令就判定通过。

### C-CHECK：应用测试

在 ARM 构建机仓库根目录执行。Dockerfile 的 test 阶段运行迁入的 Go/Vitest 测试与 UI 构建；只为节点隔离、URL 前缀和停止恢复等变化补必要测试，不复制测试框架。

```bash
test "$(uname -m)" = aarch64
docker build --target test \
  -f workspace/infra/frontend/Dockerfile -t uestcradar/frontend:test .
```

Dockerfile 固定 linux/arm64 并校验构建架构，原生 ARM 主机无需 Docker CLI 的实验性 --platform 开关；未修改宿主配置。Web 集成发生后回归其实际改动，不为未修改的 Web 重复构建。

### C-DEPLOY：每案例一次完整验收

进入相应案例目录，在无额外环境文件/业务变量时执行：

```bash
cd workspace/examples/KT2  # 或 workspace/examples/KT3
docker compose config --quiet
docker compose config --images
docker compose pull
docker compose up -d --no-build
docker compose ps
```

此处显式 pull 为取得 Harbor 复验记录，日常仍只需第 5 节的一条启动命令。每案例同一次运行完成：

1. 检查 digest、实际 ARM64 架构、单一 project、IPC/依赖与默认端口；保留 Worker/Sink 原结果校验。
2. 浏览器观察至少 60 秒：KT2 为 IQ `1:3` → 脉压 `2:2`，KT3 为脉压 `2:2` → RD `3:2`；输入/输出各有至少两个不同有效 frame_id，实际绘图，节点/Leg 不串数据。
3. 验证关闭浏览器、慢预览消费者和 Frontend 停止/恢复：停 30 秒时主链计数仍增长，恢复后 30 秒内出现有效预览，Worker/Sidecar ID 与启动时间不变。只操作目标 Frontend，不对整个 project 执行 down。
4. 保存镜像引用、命令结果、关键帧/截图及处理计数到对应案例的一份证据。不再为拉取复验、故障隔离另起一轮部署或另写测试工具；优先复用已有预览测试。

### C-SCOPE：保护已有代码与数据

F00 一次记录起点 HEAD、工作区状态与差异；用 `sha256sum` 记录被 Git 忽略的 GFKD/CSV。沿用该基线到收尾，不重新采集来掩盖改动，不开发检查器：

```bash
: "${RUN_DIR:?设置本次验收记录目录}"
mkdir -p "$RUN_DIR"
git rev-parse HEAD > "$RUN_DIR/base-ref.txt"
git status --short > "$RUN_DIR/status-before.txt"
git diff --binary HEAD > "$RUN_DIR/diff-before.patch"
sha256sum workspace/examples/KT3/algorithm/GFKD_V1_ARM \
  workspace/examples/KT3/algorithm/*.csv > "$RUN_DIR/algorithm.sha256"
```

F21 只检查，不重建基线：

```bash
sha256sum -c "$RUN_DIR/algorithm.sha256"
read -r BASELINE_REF < "$RUN_DIR/base-ref.txt"
git diff "$BASELINE_REF" --name-status
git ls-files --others --exclude-standard
git diff --check
```

对照原差异审阅全部改动和新增文件：只能触及第 1 节允许范围，算法/协议不能改变。记录不得含密码、私钥或大型原始 IQ。

## 8. 单机阶段收尾

F21 汇总规格 F01–F08：页面、数据与原校验正确，根路径/代理前缀可用，ARM64 Harbor 部署和旁路隔离通过，保护范围未越界。已有案例证据直接复用，不重复跑一套“交付审计”。未通过项如实保留，不降低门槛，也不声称原生性能或无损采集。

## 9. 最终交付：Web 内嵌与服务器结果

G01 规格与实施计划已获确认，按 HTTPS 方向实施 Web 同源嵌入与节点部署，使用已验证的同一 Frontend 镜像，不复制渲染代码。把更新后的 Web 部署到 `192.162.2.64`，通过它部署选定服务器并检查真实链路。

保留多机默认 strict-RDMA、显式 TCP 选择及禁止静默降级；不增加 RDMA 性能压测。对同一案例配置核对节点、输入/输出类型、数值与坐标语义、原算法结果校验；不是比较不同运行时刻截图是否逐像素相同。

服务器内嵌与单机直达均显示真实结果，Web 管理正常、Frontend 故障不影响主链与 Web 全局状态，才算整体完成。部署说明随这两阶段交付，不再另设第三个部署配置模块。

## 10. G01 实施细化（已获实施确认）

依据：[Web 集成规格](../SPEC-web-frontend-integration.md)。以下按现有代码拆分，进度见 todo；不将部署参数缺失当作硬件阻塞，也不借此修改宿主或放宽安全要求。

### 代码依据与最小实现

- `orchestration/http.go` 已提供 authorize、sameOrigin 和节点检查结果；新前缀在这些边界内处理，不能另外开匿名代理。
- `session.go` 有 Get 超时、Delete 和定时 expire 三条失效路径，目前只清除凭据。先统一增加取消通知，再让预览 HTTP/WS 和节点遥测订阅响应取消；不改变滑动 TTL 或 SSH 认证方式。
- `httputil.ReverseProxy` + 标准 HTTP Transport 直接访问固定节点管理 IP 的 HTTPS 8081；节点/IP/端口只来自受信配置。原始 Cookie/Authorization 不转发，TLS 保留加密但按内网模式关闭节点证书校验，拒绝跨 Origin 与路径逃逸，不在代理中调用 SSH。
- Web 的 Store/Hub 提供限定节点的 snapshot/WS；全局 `/ws` 保留原行为。通过 server 注入必要处理入口，避免 orchestration 反向导入 server 形成循环。
- 每次部署用 Go 标准库生成自签证书，经 SFTP 上传版本目录；Frontend 的 SSL_CERT_FILE 指向自身证书以支持本地 healthcheck。没有 CA/用户配置项，不重新发布 Frontend。
- 现有远程部署模板添加 Frontend、只读 TLS 文件挂载和固定 digest；SSH 仍只执行检查、拉取、上传配置与启停。私钥不放进 node.env、任务输出或镜像；node.env 仅保存自动 revision，文件写完才更新配置。
- 详情改用同源 iframe 后，分批删除旧预览 UI/协议代码、后台接收器及 9901 入口；只在完整切换验证后发布 Web。不增加双实现开关，不为清理无用依赖修改 UI lockfile、重建基座。
- 现有 planner 只有 strict-RDMA 路径。按既定规格补显式 TCP 选择时，默认仍为 strict-RDMA；TCP 的 Compose 不挂 RDMA 设备，失败不得自动切换模式。

### 实施顺序

G03 会话取消 → G04 HTTPS 代理 → G05 节点遥测 → G06 部署配置 → G07 显式 transport → G08 iframe 与旧 UI 清理 → G09 旧后端移除 → G10 ARM 集成验证 → G11 发布/部署准备 → G12 实际服务器验收。

这里只安排一个执行链，不委派子代理、不同时安排多个写入者。已有本机验收不重复；只有 Frontend 运行代码改变才补必要回归并发布新 digest。

### ARM 构建与测试

从已确认源码版本，在原生 ARM 构建机仓库根目录执行：

```bash
test "$(uname -m)" = aarch64
docker build --target builder \
  --build-arg GO_BASE=registry.chengyistudio.com/cxx/web@sha256:52c3755b78f07a64e28b95efccf3d4c70ac97808b65b51bc1e8f8a7829c2835b \
  -f workspace/infra/web/Dockerfile -t uestcradar/web:integration-test .
docker run --rm --network none --entrypoint sh uestcradar/web:integration-test \
  -ec 'go test -count=1 ./... && go vet ./... && cd frontend && npm test'
docker run --rm --network none --entrypoint sh uestcradar/web:integration-test \
  -ec 'CGO_ENABLED=1 go test -race ./internal/orchestration ./internal/server'
```

测试证书仅用于测试，不能成为部署 CA 或写入运行镜像。验证受信/过期/错误 SAN/未知 CA、HTTP 与 WS 代理、节点隔离、慢消费者、会话失效和超时；真实服务器连通性不由 mock 测试抵扣。

### 发布和部署前必须落实

1. 从 Web 已有节点列表自动探查选择，Web 固定 `.64`。当前模型每个 IP 一个节点，按 KT2/KT3 链路所需数量选择，不覆盖已有业务。
2. 节点自签证书自动生成安装，私钥 65532:65532/0400，只读挂载；无需用户提供证书/CA。
3. 无 8081 白名单前提，不改宿主防火墙/Docker/驱动。仅供可信内网；当前仍需要可用的原有 SSH 凭据与连接条件。
4. 检查现有工作负载与端口。需要替换时单独确认；没有权限或参数就暂停部署，不暂停可独立完成的本地代码/测试。
5. Web 新版本先通过 ARM 验证，再以未占用的不可变标签发布 Harbor并拉回检查；沿用既有发布流程，不增加工具链。

G12 使用 Web 实际部署与观测两条链，记录节点、镜像、运输方式、图像和原校验结果，并完成 30 秒隔离/恢复。G03–G11 通过都不能代替 G12；G01 只在所有实际交付完成后关闭。
