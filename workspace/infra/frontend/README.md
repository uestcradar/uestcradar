# 独立节点 Frontend

同一 Go + React 应用用于单机直接访问和后续 Web 同源内嵌。复用原预览协议、波形/RD 绘图与遥测；不包含 SSH、Docker 编排或账号系统。

当前状态：应用已实现，ARM64 构建/单元测试及进程冒烟验证通过；**尚未发布 Harbor、尚未完成 KT2/KT3 实际链路及 Web 集成验收**。任务见 [todo](../tasks/todo.md)。

## 部署入口（案例 Compose 交付后）

在对应案例目录执行：

```bash
docker compose up -d --no-build
```

Compose 将包含经验证的 ARM64 Harbor digest 与节点默认配置；无需部署环境文件。KT2 算法页默认 `http://127.0.0.1:8082`，KT3 RD 页默认 `http://127.0.0.1:8083`。目前这些 Compose 尚未交付，不将应用健康检查当作案例验收。

## 节点配置

| 环境变量 | 默认值 / 用途 |
|---|---|
| NODE_ID | 必填；只接纳该节点的遥测与预览 |
| TELEMETRY_HTTP_ADDR | 127.0.0.1:8081，页面、HTTP 与浏览器 WebSocket |
| TELEMETRY_UDP_ADDR | 127.0.0.1:9902，Sidecar 遥测 |
| PREVIEW_TCP_ADDR | 127.0.0.1:9903，Sidecar 预览 |
| TELEMETRY_TLS_CERT_FILE / TELEMETRY_TLS_KEY_FILE | 成对配置；非 loopback HTTP 默认要求 TLS |
| TELEMETRY_ALLOW_INSECURE_HTTP | 默认 false；沿用既有显式 HTTP 例外，不自动放宽 |

Sidecar 使用已有 TELEMETRY_HOST/PORT、PREVIEW_HOST/PORT 指向上述监听器。Frontend 不读主数据 Ring，不加入 Worker IPC。服务器访问限制及 Web 代理由后续集成阶段配置。

接口：`/api/node` 返回当前 Sidecar 实例/streams；`/api/snapshot`、`/ws` 为节点遥测；`/ws/frames` 为二进制预览；`/healthz` 与 `/frontend --healthcheck` 只检查服务就绪。页面使用相对 URL，代理入口必须规范为以 `/` 结尾，并剥离前缀后转发 HTTP/WebSocket。

断开时清除预览；超过 3 秒未更新的帧标为非实时。帧 ID 与丢弃计数按十进制字符串保留 uint64 精度。渲染坐标、池化与色标沿用原实现。

## 构建、测试与发布（维护者，不是日常部署步骤）

在原生 ARM64 构建机、仓库根目录运行。Dockerfile 固定 ARM64 基座并校验运行架构，无需依赖 Docker CLI 的实验性跨平台开关；完整镜像构建也必须经过 test 阶段。

```bash
test "$(uname -m)" = aarch64
docker build --target test -f workspace/infra/frontend/Dockerfile -t uestcradar/frontend:test .
```

正式发布前须取得许可，使用干净且已确认的源码版本，并确认 Harbor 版本标签未占用；认证/网络错误不等于标签不存在。以下只在上述条件满足后执行，不自动提交源码或覆盖已有标签：

```bash
test -z "$(git status --porcelain)"
REV=$(git rev-parse --short=12 HEAD)
IMAGE="registry.chengyistudio.com/cxx/frontend:sha-${REV}-arm64"
docker build -f workspace/infra/frontend/Dockerfile -t "$IMAGE" .
test "$(docker image inspect --format '{{.Architecture}}' "$IMAGE")" = arm64
docker image inspect --format '{{json .Config.Entrypoint}} {{.Config.User}}' "$IMAGE"
docker push "$IMAGE"
docker pull "$IMAGE"
docker image inspect --format '{{json .RepoDigests}}' "$IMAGE"
```

记录实际 digest、源码版本、测试和运行结果；本机通过 Harbor 拉取该 digest 后再运行案例。不得用构建机临时 tag、image ID 或 save/load 替代发布镜像验收。只部署该版本，不另维护滚动标签或发布包装脚本。
