# Sidecar 基础镜像构建与推送指南

本文档说明如何构建和推送 Sidecar 的双基础镜像 (`build-base` 与 `runtime-base`)。通过预建基础镜像，可以实现 Sidecar 的**秒级增量编译**与**生产运行镜像极度瘦身 (~50MB)**。

---

## 📦 镜像概览

| 镜像 Tag                           | 配置文件                           | 作用与特点                                                                                                                                                 |
| :--------------------------------- | :--------------------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`sidecar:build-base`**   | `docker/Dockerfile.build-base`   | **编译基础镜像**：预装 C++20 工具链 (`g++`, `cmake`, `ninja`) 以及 `libucx-dev`、`libprotobuf-dev` 等头文件与静态/动态库，专用于编译阶段。 |
| **`sidecar:runtime-base`** | `docker/Dockerfile.runtime-base` | **运行时基础镜像**：仅保留 `libucx0` 和 `ucx-utils` 动态库，零编译器，体积仅约 50MB，专用于生产环境部署。                                        |
| **`sidecar:latest`**       | `Dockerfile`                     | **主运行镜像**：通过多阶段构建生成的最终 Sidecar 生产容器。                                                                                          |

---

## 🚀 构建与推送流程

在项目根目录（`/home/zikun/code/common/uestcradar`）下运行以下命令：

### 1. 构建并推送 `build-base` 编译基础镜像

```bash
# 构建 build-base
docker build -t registry.chengyistudio.com/cxx/sidecar:build-base -f workspace/sidecar/docker/Dockerfile.build-base .

# 推送到私有镜像仓库
docker push registry.chengyistudio.com/cxx/sidecar:build-base
```

---

### 2. 构建并推送 `runtime-base` 运行时基础镜像

```bash
# 构建 runtime-base
docker build -t registry.chengyistudio.com/cxx/sidecar:runtime-base -f workspace/sidecar/docker/Dockerfile.runtime-base .

# 推送到私有镜像仓库
docker push registry.chengyistudio.com/cxx/sidecar:runtime-base
```

---

### 3. 使用预建基础镜像构建 Sidecar 业务镜像

在根目录下使用主 `Dockerfile` 进行构建与推送：

```bash
# 构建最终的 sidecar:latest
docker build \
  --build-arg BUILD_BASE=registry.chengyistudio.com/cxx/sidecar:build-base \
  --build-arg RUNTIME_BASE=registry.chengyistudio.com/cxx/sidecar:runtime-base \
  -t registry.chengyistudio.com/cxx/sidecar:latest \
  -f workspace/sidecar/Dockerfile .

# 推送最终运行镜像
docker push registry.chengyistudio.com/cxx/sidecar:latest
```
