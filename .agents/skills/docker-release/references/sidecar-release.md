# Sidecar 旁路网关双 Tag 镜像发布与服务器启动指南

本文档详细说明如何针对生产环境编译、打标并发布 `sidecar` 镜像，以及在服务器侧仅启动 `sidecar-node` 的操作步骤。

---

## 1. 核心发布规则

- **双 Tag 原则**：
  - `dual-leg-${GIT_SHA}-arm64`：不可覆盖的版本 Tag（用于审计、追溯和应急回滚）。
  - `latest`：生产覆盖 Tag（更新推送后，服务器配置无需更改）。

---

## 2. 标准构建、双 Tag 打标与推送指令

在项目根目录执行：

```bash
# 环境变量设置
export REGISTRY="registry.chengyistudio.com/cxx"
export GIT_SHA="$(git rev-parse --short=12 HEAD)"
export PLATFORM="linux/arm64"

export VERSION_IMAGE="${REGISTRY}/sidecar:dual-leg-${GIT_SHA}-arm64"
export LATEST_IMAGE="${REGISTRY}/sidecar:latest"

# 1. 编译版本镜像
docker buildx build \
  --platform "$PLATFORM" \
  --target runtime \
  -t "$VERSION_IMAGE" \
  --load \
  -f workspace/sidecar/Dockerfile .

# 2. 关联打标为 :latest
docker tag "$VERSION_IMAGE" "$LATEST_IMAGE"

# 3. 登录并同时推送两个 Tag
docker login registry.chengyistudio.com
docker push "$VERSION_IMAGE"
docker push "$LATEST_IMAGE"
```

---

## 3. 服务器端 Sidecar 拉取与启动步骤

假设服务器端环境变量与 Compose 配置文件已配置完成，仅预拉取并启动 Sidecar 节点：

```bash
# 1. 预拉取最新 Sidecar 镜像
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" -f "$COMPOSE" pull

# 2. 启动 Sidecar 节点 (不触发本地 build)
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" -f "$COMPOSE" up -d --no-build sidecar-node

# 3. 查看 Sidecar 日志确认建链与遥测状态
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" -f "$COMPOSE" logs --no-color sidecar-node
```
