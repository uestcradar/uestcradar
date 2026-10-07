# Plan：Frontend 独立部署与后续 Web 集成

状态：**按用户要求推进至 Tasks，计划及清单待审；未开始实现。**
当前详细实施范围：`frontend-runtime`。依据：[规格](../SPEC-frontend-runtime.md)、[能力图](../CAPABILITY_MAP.md)。执行清单：[todo.md](todo.md)。

## 1. 最新修改边界

- 功能源码、测试脚本、Dockerfile、发布入口、Compose override **只允许新增/修改 `workspace/infra/frontend/` 和 `workspace/infra/web/`**。
- KT2/KT3 的源码、测试、算法编译产物、CMake、原 Compose 和原 Dockerfile 全部只读。
- Sidecar、SDK、Ring、协议文件、公共 `.agents/skills/docker-release/` 脚本均只读；复用其行为，不直接修改。
- 文档例外：本次能力图、规格和 `infra/tasks/`。之前工作区整理留下的修改不属于本任务，不回滚、不混入范围检查。
- 若实际实现必须越界，暂停并说明原因，不自行增加白名单。

## 2. 实施顺序与依赖

```text
F00 基线保护
 └─ F02 Harbor/镜像预检（还依赖已完成的 F01 ARM64 环境）
     ├─ F03 Go 配置 → F04 预览接收 → F05 节点绑定
     │              └─ F06 遥测状态
     └─ F07 UI 构建 → F08 波形解码 → F09 RD/预览组件 → F10 节点页面
               F03–F10 → F11 独立 HTTP/WS/健康检查
                         → F12 ARM64 Docker
                         → F13 目录内发布入口 → F14 发布与拉取
                         → F15 KT2 override → F16 KT2 本机验收
                         → F17 KT3 override → F18 KT3 本机验收
                         → F19 故障隔离 → F20 拉取复验 → F21 交付审计
                         → G01 Web 内嵌与 .64 多机集成规格
                         → G02 部署配置收敛规格
```

后端 F03–F06 与 UI F07–F10 在技术上可并行，不代表授权调用子代理。默认单人顺序执行，任何同时写相同文件的工作必须串行。KT2、KT3 的运行验收顺序执行，避免端口和旧容器干扰。

G01/G02 仅为后续审批关卡，不提前生成未有规格依据的实现任务。第一阶段必须真正跑通两个本机案例，不能延后到服务器才证明预览有效。

## 3. 最小实现方案

### Frontend 应用

- 新应用位于 `infra/frontend/`，使用 Go 1.24 + React/TypeScript 和现有依赖版本，不引入微前端框架。
- Go module 拟为 `uestcradar/frontend`，入口 `cmd/frontend`；静态页面放 `ui/`，产物进入 `web/dist/`，由 Go embed 提供。
- 复用现有 preview、遥测 store/hub、波形/RD 解码和渲染代码。现有 Go `internal` 包不能由另一 module 直接导入：第一阶段将必要实现及原测试迁入 Frontend 所有的包，暂时保留旧 Web 副本以维持旧入口。第二阶段切换内嵌后删除 Web 的旧预览实现，禁止两套长期独立演进。
- 不新建 `infra/common` Go module 或共享组件框架；协议仍读取原 `infra/proto`，生成代码仅落在允许目录中，Go package 映射可调整，线协议不变。
- 保留精度、池化、坐标轴和预览丢弃语义，不重新实现一套算法结果渲染。

### 节点接口

- `NODE_ID` 必填，一实例对应一节点；不同节点的数据不能混入同一实例。
- `GET /api/node` 返回本节点 ID、Sidecar 实例、连接状态和来自 SidecarHello 的 streams。无需从 Web 镜像编排接口获取输入/输出类型。
- 沿用节点遥测快照与 `/ws`；`/ws/frames` 保持现有二进制预览协议。
- `/healthz` 只表示进程及监听器就绪，不表示已收到结果。提供镜像内 `frontend --healthcheck` 检查，不要求 scratch 镜像额外安装 curl。
- `SidecarHello` 断开/替换时更新实例状态；预览类型、节点与消息长度由后端校验，前端明确标识等待/断开，不展示过期帧为实时结果。
- UI 的资源及接口 URL 支持根路径和代理前缀。先验证可嵌入性，本阶段不接管 Web 路由。

### 观测与访问限制

- 本机：Sidecar 的遥测与预览均发送给对应 Frontend；页面仅监听 loopback，不新增密码或账号系统。
- 后续服务器：Sidecar 全局遥测继续送 Web，预览送节点 Frontend；Web 仍独立掌握状态和部署能力。嵌入页面需要的遥测复用 Web 已有数据接口，具体连接方式由 G01 规格确定，不把 Frontend 变成全局遥测必经中继。
- 服务器 Frontend 端口仅允许 Web 主机访问；浏览器走已有 Web 会话和同源代理，不将 SSH 密码、私钥或会话凭据发给 Frontend。
- 不修改 Sidecar 数据面或协议。现有 `TELEMETRY_HOST/PORT`、`PREVIEW_HOST/PORT` 足够表达独立接收位置。

## 4. 端口与实例表

Web 保留 HTTP `8080/TCP`、旧遥测 `9900/UDP` 和旧预览 `9901/TCP`。下表是 **host 网络模式下的默认建议值**，均可通过 Docker 环境配置覆盖，启动前检查占用。

| 案例 | Frontend 服务名 | 绑定的现有 NODE_ID | 页面/浏览器 WS TCP | 遥测 UDP | Sidecar 预览 TCP |
|---|---|---|---|---|---|
| KT2 | frontend-source | local-source | 8081 | 9902 | 9903 |
| KT2 | frontend-operator | local-pulsecompression | 8082 | 9904 | 9905 |
| KT2 | frontend-sink | local-sink | 8083 | 9906 | 9907 |
| KT3 | frontend-source | local-iq-source | 8081 | 9902 | 9903 |
| KT3 | frontend-pulsecompression | local-pulsecompression | 8082 | 9904 | 9905 |
| KT3 | frontend-rd | local-qt5-rd-algorithm | 8083 | 9906 | 9907 |
| KT3 | frontend-sink | local-rd-sink | 8084 | 9908 | 9909 |

每台服务器只部署一个逻辑节点时，Frontend 可统一使用 8081/9902/9903，IP 不同不会冲突。这里不改变两案例原有 UCX 端口与 SHM 名称。两套本机案例默认不同时运行。

## 5. 原案例只读：使用 Compose override

新增文件全部位于 `infra/frontend/docker/`：

```text
kt2-infra.override.yaml
kt2-worker.override.yaml
kt3-infra.override.yaml
kt3-worker.override.yaml
deploy.env.example
```

- 每次先指定原 Compose，再指定对应 override，保留原 project、IPC 和相对构建路径解析，不把 worker/infra 两个 project 粗暴合并。
- infra override 新增 Frontend，并只覆盖 Sidecar 观测地址/端口及审定后的镜像引用。不改变算法参数、帧契约与结果校验。
- worker override 覆盖 `image` 为 Harbor digest。原配置的 `build:` 可以保留，但正式启动必须 `--no-build`，不能由启动隐式触发构建。
- 不使用旧 Compose 不支持的 `!reset` 等标签。override 不包含源码挂载或依赖案例目录的相对资源路径。
- Frontend 不能成为数据链健康检查/启动的强依赖；停止 Frontend 不得级联停止 Sidecar 或 Worker。
- 环境文件仅含镜像和端口等非敏感配置，镜像缺失就报错，不回退到本地 `:dev`。

## 6. ARM64 构建、发布与环境

- 所有业务构建、测试、运行容器为 `linux/arm64`。宿主上的 Compose、文件哈希等编排检查不是 AMD64 业务镜像替代测试。
- F01 已经用户独立授权先完成：使用现有宿主架构 binfmt 安装工具一次性注册 qemu-aarch64，随后 ARM64 业务基座实际执行通过。该安装工具不是 AMD64 应用镜像替代。运行时注册未设开机持久化，宿主重启后重新检查；其他宿主变更仍须确认。
- 复用已有 ARM64 Web build-base 与 lockfile 缓存；若缓存需要更新，只修改允许范围内的 `web/docker/`，并回归原 Web 构建。
- 新 Frontend 发布入口放 `frontend/docker/release.sh`，镜像校验放同目录；公共发布脚本不改。沿用既有规则：ARM 发布环境、审定源码版本、契约/架构校验、不可变标签不覆盖、推送并拉回验证、记录 manifest digest。
- Frontend 镜像拟使用独立 `frontend/v1` label 与 `/frontend` 入口，不冒充 `web/v1` 或 Worker 契约来绕过校验。
- ARM 发布机可按受控流程构建并推送；之后包括 `.64` 在内的验收部署阶段只拉取启动。禁止临时现场构建、save/load 或源码挂载替代 Harbor 复验。
- 发布前需要用户确认源码提交与发布操作；不自动提交当前混合工作区，也不自动覆盖已有运行容器。

## 7. 验证入口

下列新脚本属于待实现产物，不表示当前已存在或已通过。任务执行时记录真实命令、输出、退出码及时间。

### C-ARM：最小 ARM64 探测

```bash
: "${ARM_PROBE_IMAGE:?设置审定的 ARM64 基座镜像引用}"
docker run --rm --pull=never --platform linux/arm64 --network none \
  --entrypoint /bin/sh "$ARM_PROBE_IMAGE" -c 'test "$(uname -m)" = aarch64'
```

### C-CHECK：应用测试与构建

`check.sh` 自身使用 shell 编排，backend/ui/all/web 子命令必须启动 ARM64 构建容器，不在 x86 宿主直接运行 Go/Node 测试。

```bash
bash workspace/infra/frontend/docker/check.sh backend
bash workspace/infra/frontend/docker/check.sh ui
bash workspace/infra/frontend/docker/check.sh all
bash workspace/infra/frontend/docker/check.sh web
```

### C-DEPLOY：叠加配置、拉取与无构建启动

从仓库根目录执行；先用 KT2，再将 `CASE=kt3`、`BASE=workspace/examples/KT3/docker` 执行同一段。`DEPLOY_ENV` 为对应案例已审阅的环境文件绝对路径。

```bash
: "${DEPLOY_ENV:?设置已审阅的环境文件绝对路径}"
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

### C-SCOPE：修改边界检查

保护清单脚本放 Frontend 内，不改案例。在 F00 记录当前基线，之后只能与同一基线比较；不得重新采集基线来掩盖越界修改。基线包含已有未提交差异、原四份 Compose、算法源码以及被 Git 忽略的 GFKD/CSV 指纹。

```bash
python3 workspace/infra/frontend/tests/check_scope.py capture --output "$RUN_DIR/scope.json"
python3 workspace/infra/frontend/tests/check_scope.py verify --baseline "$RUN_DIR/scope.json"
```

`RUN_DIR` 是任务专用日志目录；公开证据只保留脱敏结果，不把密码、私钥、完整环境或大型原始 IQ 纳入 Git。

## 8. 检查点、风险与退路

| 检查点 | 必须证明 | 失败处理 |
|---|---|---|
| 环境 | ARM64 容器在本机可运行，Harbor 引用可拉取 | 记录原始错误；不换架构、不转移验收地点冒充本机 |
| 应用 | 协议、节点绑定、真实渲染、子路径可用，原 Web 无回归 | 先修该层，不改算法或帧契约绕过 |
| 镜像 | 构建测试与运行均 ARM64，发布与拉取 digest 一致 | 禁止仅凭本地 image ID 或缓存标签判定通过 |
| KT2 / KT3 | 各自 60 秒真实输入输出、至少两个有效 frame_id、原校验通过 | 保留日志；空白页、健康检查、mock 不能替代 |
| 隔离 | Frontend 停止 30 秒数据计数增长，恢复后 30 秒内恢复预览 | 查旁路连接/重试，不改变数据面背压规则 |
| 范围 | KT2/KT3、Sidecar、SDK、协议和公共发布脚本未改 | 停止、报告；不得擅自修白名单 |

当前已知：GFKD ARM64 产物及 CSV 在本机，已有 KT3 Worker 镜像缓存；不能再次把“Git 忽略”当作“文件缺失”。模拟环境性能不足时报告，不擅自降低规格 F03/F05 门槛。

应用回滚使用上一个已验证 digest；测试清理只停止本次启动的 project/服务，先保存证据，不执行 Docker prune，不影响已有部署。

## 9. 后续模块关卡

- **G01 / web-frontend-integration**：F21 通过后审定该模块规格。Web 仍部署在 `.64`，通过现有 SSH/Compose 管理选定节点；同一 Frontend 页内嵌入、HTTP/WS 同源代理、原会话保护、ARM64 Harbor 拉取和多机链路结果一起验收。不是只在 `.64` 打开一个静态页面，也不要求多份 Web。
- **G02 / deployment-profiles**：G01 验收后整理两套默认配置和实际使用过的镜像清单，复验独立部署、服务器部署与显式覆盖，不另引入微前端平台或 RDMA 性能压测。

本环境未找到 skill 引用的 `planning-and-task-breakdown` 文件。本轮使用已提供的 spec-driven-development 内置规则：依赖有向无环、按可验证结果切片、单任务原则上不超过约五个源文件；没有声称读取未安装的技能。
