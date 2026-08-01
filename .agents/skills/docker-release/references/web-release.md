# Telemetry Web 双 Tag 镜像发布与独立运行指南

本文档详细说明如何针对生产环境编译、打标并发布版本 Tag 与 `:latest` 覆盖 Tag 的 `telemetry-web` 镜像，以及如何使用原生 `docker run` 完全对齐 Compose 生产安全参数独立拉起该服务。

---

## 1. 核心发布规则

- **双 Tag 原则**：
  - `dual-leg-${GIT_SHA}-arm64`：审计版本 Tag。
  - `latest`：生产最新 Tag。

---

## 2. 标准构建、双 Tag 打标与推送指令

在项目根目录执行：

```bash
# 环境变量设置
export REGISTRY="registry.chengyistudio.com/cxx"
export GIT_SHA="$(git rev-parse --short=12 HEAD)"
export VERSION_IMAGE="${REGISTRY}/telemetry-web:dual-leg-${GIT_SHA}-arm64"
export LATEST_IMAGE="${REGISTRY}/telemetry-web:latest"

# 1. 编译版本镜像
# 方法 A：支持 buildx 的环境
docker buildx build --platform linux/arm64 -t "$VERSION_IMAGE" --load -f workspace/web/Dockerfile .

# 方法 B：物理 ARM64 目标服务器原生构建 (无 buildx 插件)
docker build -t "$VERSION_IMAGE" -f workspace/web/Dockerfile .

# 2. 关联打标为 :latest
docker tag "$VERSION_IMAGE" "$LATEST_IMAGE"

# 3. 登录并推送
docker login registry.chengyistudio.com
docker push "$VERSION_IMAGE"
docker push "$LATEST_IMAGE"
```

---

## 3. 服务器端原生 `docker run` 完整安全参数运行

```bash
# 1. 拉取最新的 web 镜像
docker pull registry.chengyistudio.com/cxx/telemetry-web:latest

# 2. 强行停止并清理旧容器（如果存在）
docker rm -f telemetry-web 2>/dev/null || true

# 3. 完整参数独立拉起服务 (对齐 compose.yaml 的安全与资源限制)
docker run -d \
  --name telemetry-web \
  --restart always \
  -p 8081:8080 \
  -p 9900:9900/udp \
  --read-only \
  --cap-drop=ALL \
  --security-opt no-new-privileges:true \
  --memory 128m \
  --cpus 0.50 \
  registry.chengyistudio.com/cxx/telemetry-web:latest

# 4. 查看运行日志确认
docker logs -f telemetry-web
```
