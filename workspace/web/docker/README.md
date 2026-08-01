# Telemetry Web 镜像分工构建与发布指南

本文档说明 Telemetry Web 组件的镜像架构分工与构建流程。通过职责分离，**基础编译镜像在 x86 机器上一次性生成**，**ARM 服务器仅负责从私有源拉取基础镜像并进行增量业务代码编译**，实现完全离线、秒级无依赖构建。

---

## 📦 镜像架构与分工边界

| 镜像 Tag | 配置文件 | 存放与构建地点 | 职责说明 |
| :--- | :--- | :--- | :--- |
| **`telemetry-web:build-base`** | `docker/Dockerfile.build-base` | **x86 构建机上构建并推送** | **通用编译基础镜像**：预装 Go 1.24、`protobuf-compiler` 与 `protoc-gen-go`。必须在 x86 机器上使用 `$BUILDPLATFORM` 构建好并推送到私有源，**严禁在物理服务器上构建此镜像**。 |
| **`telemetry-web:latest`** | `Dockerfile` | **ARM 服务器 / x86 机器上构建** | **主业务运行镜像**：直接基于私有源的 `telemetry-web:build-base` 拉取后增量编译 Web 业务代码，打包为极简生产镜像（约 15MB）。 |

---

## 🚀 步骤一：在 x86 交叉构建机上构建并推送 `build-base` 基础镜像

在 x86 机器的项目根目录下运行（仅需在环境升级或依赖变更时运行一次）：

```bash
export REGISTRY="registry.chengyistudio.com/cxx"

# 利用 $BUILDPLATFORM 原生交叉编译 protoc-gen-go，并推送到私有源
docker buildx build \
  --platform linux/arm64 \
  --progress=plain \
  -t "${REGISTRY}/telemetry-web:build-base" \
  --push \
  -f workspace/web/docker/Dockerfile.build-base .
```

---

## ⚡ 步骤二：在 ARM64 物理服务器（如 node4-1）上仅构建业务镜像

在 ARM64 物理服务器的项目根目录下，无需访问外网 Docker Hub，**直接从私有源拉取 `build-base` 镜像进行业务代码增量编译与推送**：

```bash
export REGISTRY="registry.chengyistudio.com/cxx"

# 1. 优先从私有源拉取预构建好的 build-base 编译基础镜像
docker pull "${REGISTRY}/telemetry-web:build-base"

# 2. 直接基于私有源基础镜像进行 Web 业务代码编译与打包
docker build \
  --build-arg GO_BASE="${REGISTRY}/telemetry-web:build-base" \
  -t "${REGISTRY}/telemetry-web:latest" \
  -f workspace/web/Dockerfile .

# 3. 推送生产运行镜像至私有源
docker push "${REGISTRY}/telemetry-web:latest"
```
