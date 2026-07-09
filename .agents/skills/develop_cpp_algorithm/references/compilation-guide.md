# 编译指南 (Compilation Guide)

在算法开发（Develop）阶段，为了确保算子能顺利编译通过并开展本地仿真自检，我们统一在本地 X86 开发隔离容器中执行编译。

---

## 📌 本地同构编译 (X86 开发机 -> X86 容器)

本地编译适用于在开发机上进行算子逻辑实现验证与仿真单测。我们使用专属本地开发镜像 `uestcradar-build:local`：

### 1. 自动构建本地编译镜像 (仅首次需要)
若本地未检测到 `uestcradar-build:local`，部署系统或您可手动执行以下命令，基于技能包预置的 Dockerfile 构建自检底座：
```bash
docker build -t uestcradar-build:local \
  -f .agents/skills/cpp_algorithm_ops/Dockerfile.build \
  .agents/skills/cpp_algorithm_ops
```

### 2. 挂载工作区执行隔离编译
在项目根目录下，执行以下隔离编译指令。此步骤会将本地工作区挂载进容器，并将动态插件库 `.so` 及单测程序直接输出到 `cpp/build/` 下：
```bash
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd):/workspace" \
  -w /workspace/cpp \
  uestcradar-build:local \
  bash -lc "cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
```
编译产出为 `cpp/build/lib/` 下的各算子插件及 `cpp/build/bin/` 下的单测二进制可执行程序。

---

## 🔗 异地物理机交叉编译

当本地开发自检完成，需要将算子插件推送到 ARM64 物理板卡（如 `node2`、`node4` 等分布式节点）时，必须执行跨平台交叉编译及网络分发。

* **交叉编译与物理分发详阅**：**[Phase 2: Docker隔离编译与交叉编译 (cpp_algorithm_ops)](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/references/phase2-compilation.md)**。
