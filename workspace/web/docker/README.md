# Telemetry Web 基础镜像构建与推送指南

本文档说明如何构建和推送 Telemetry Web 组件的通用编译基础镜像 (`build-base`)。通过预建基础镜像，可以实现 Web 组件的**离线构建**与**秒级增量编译**，避免在无外网连接的物理服务器上因访问外网 Docker Hub 而导致构建失败。

---

## 📦 镜像概览

| 镜像 Tag | 配置文件 | 作用与特点 |
| :--- | :--- | :--- |
| **`telemetry-web:build-base`** | `docker/Dockerfile.build-base` | **编译基础镜像**：预装 Go 1.24 工具链、`protobuf-compiler` 以及 `protoc-gen-go` 代码生成插件，专用于编译 ARM64 / x86 架构的 Go 遥测 Collector 服务。 |
| **`telemetry-web:latest`** | `Dockerfile` | **主运行镜像**：基于 `scratch` 极简运行环境生成的 ARM64 生产镜像（体积仅约 15MB）。 |

---

## 🚀 跨平台构建与自动推送流程（在 x86 构建机上打 ARM64 镜像）

在项目根目录下运行以下命令（使用 `--push` 参数实现一键编译并自动推送到私有仓库）：

### 1. 构建并自动推送 `build-base` 编译基础镜像

```bash
export REGISTRY="registry.chengyistudio.com/cxx"

# Buildx 一键编译并自动 Push 推送至私有源
docker buildx build \
  --platform linux/arm64 \
  -t "${REGISTRY}/telemetry-web:build-base" \
  --push \
  -f workspace/web/docker/Dockerfile.build-base .
```

---

### 2. 构建并自动推送 `telemetry-web:latest` 业务镜像

```bash
export REGISTRY="registry.chengyistudio.com/cxx"

# Buildx 一键编译并自动 Push 推送 ARM64 生产运行镜像至私有源
docker buildx build \
  --platform linux/arm64 \
  --build-arg GO_BASE="${REGISTRY}/telemetry-web:build-base" \
  -t "${REGISTRY}/telemetry-web:latest" \
  --push \
  -f workspace/web/Dockerfile .
```
