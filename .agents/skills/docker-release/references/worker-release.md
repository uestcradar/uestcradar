# Cascade Worker 算子双 Tag 镜像发布指南

本文档详细说明如何编译、打标并发布版本 Tag 与 `:latest` 覆盖 Tag 的 `cascade-worker` 镜像。

---

## 1. 核心规则

- **服务器固定配置**：`CASCADE_IMAGE=registry.chengyistudio.com/cxx/cascade-worker:latest`
- **双 Tag 原则**：
  - `dual-leg-${GIT_SHA}-arm64`：版本备份 Tag。
  - `latest`：生产最新部署 Tag。

---

## 2. 标准构建、双 Tag 打标与推送指令

在项目根目录执行：

```bash
# 环境变量设置
export REGISTRY="registry.chengyistudio.com/cxx"
export GIT_SHA="$(git rev-parse --short=12 HEAD)"
export VERSION_IMAGE="${REGISTRY}/cascade-worker:dual-leg-${GIT_SHA}-arm64"
export LATEST_IMAGE="${REGISTRY}/cascade-worker:latest"

# 1. 编译版本镜像
# 方法 A：支持 buildx 的环境
docker buildx build --platform linux/arm64 --target cascade-worker -t "$VERSION_IMAGE" --load -f workspace/sidecar/Dockerfile .

# 方法 B：物理 ARM64 目标服务器原生构建 (无 buildx 插件)
docker build --target cascade-worker -t "$VERSION_IMAGE" -f workspace/sidecar/Dockerfile .

# 2. 关联打标为 :latest
docker tag "$VERSION_IMAGE" "$LATEST_IMAGE"

# 3. 登录并推送
docker login registry.chengyistudio.com
docker push "$VERSION_IMAGE"
docker push "$LATEST_IMAGE"
```

---

## 3. 服务器拉取与无缝部署

```bash
docker pull registry.chengyistudio.com/cxx/cascade-worker:latest
docker-compose up -d --force-recreate worker-node
```
