# Cascade Worker 独立示例算子构建与发布指南

本示例目录演示如何脱离 SDK 源码编译，直接基于预建的基础镜像（`registry.chengyistudio.com/cxx/algo-base:latest`）进行业务算子的独立秒级编译与 Docker 镜像发布。

---

## 📦 目录结构

```text
workspace/examples/cascade_worker/
├── Dockerfile          # 基于 algo-base 基础镜像打标的 Dockerfile
├── CMakeLists.txt      # 仅编译算子业务代码的 CMake 脚本
├── README.md           # 构建说明文档
└── src/
    └── main.cpp        # 包含 source / operator / sink 角色的算子源码
```

镜像支持 `source`、`operator`、`sink` 三种角色。编排控制面通过
`CASCADE_ROLE` 环境变量选择角色；直接运行时仍可使用原有 `--role` 参数，命令行参数优先。

---

## 🚀 镜像构建与推送

在项目根目录下运行以下命令（在 x86 电脑上构建 ARM64 镜像推送到私有源）：

```bash
export REGISTRY="registry.chengyistudio.com/cxx"

# 1. 使用 buildx 构建 ARM64 算子镜像并推送
docker buildx build \
  --platform linux/arm64 \
  -t "${REGISTRY}/cascade-worker:latest" \
  --push \
  -f workspace/examples/cascade_worker/Dockerfile workspace/examples/cascade_worker

# 2. 或在物理 ARM64 服务器上直接运行原生构建与推送
cd workspace/examples/cascade_worker
docker build -t "${REGISTRY}/cascade-worker:latest" .
docker push "${REGISTRY}/cascade-worker:latest"
```
