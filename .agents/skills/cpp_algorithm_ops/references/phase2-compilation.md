# Phase 2: 基于 CPU 架构的隔离编译与交叉编译

为了确保算法插件在不同运行环境中的绝对兼容性，以及防止本地库对编译产生的重定位（Relocation）链接污染，我们统一在 Docker 隔离镜像中执行编译构建。

---

## 1. 本地同构编译 (X86 开发机 -> X86 二进制)

适用于在本地宿主机或本地 x86 容器中进行流图调试。此场景可根据您的宿主机环境分为两类运行分支：

### 分支 A：复用已有编译镜像 (开发机推荐 🚀)
若您的开发机本地**早已构建并存在 `cycore-build:latest` 镜像**（例如在 `cycore` 的日常开发流程中），您**无需重新制作镜像**，可直接执行以下挂载编译命令：
```bash
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd):/workspace" \
  -w /workspace/cpp \
  cycore-build:latest \
  bash -lc "cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
```

### 分支 B：从零制作纯净镜像 (全新部署环境)
若在全新环境部署或缓存缺失时，请先基于技能包中预置的 `Dockerfile.build` 构建本地原生编译镜像：
```bash
# 1. 制作本地同构编译镜像 (一次性构建)
docker build -t cycore-build-local \
  -f .agents/skills/cpp_algorithm_ops/Dockerfile.build \
  .agents/skills/cpp_algorithm_ops
```
```bash
# 2. 挂载 uestcradar 工作区并编译
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd):/workspace" \
  -w /workspace/cpp \
  cycore-build-local \
  bash -lc "cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
```
编译产出为 `cpp/build/lib/` 下的各类 `.so` 插件。

---

## 2. 跨平台交叉编译 (X86 开发机 -> ARM64 二进制)

适用于将插件直接推送到远端 ARM64 物理机（如 `node2`、`node4` 等物理板卡）运行环境。

在项目根目录下，首先基于预置的 Dockerfile 构建出包含 `aarch64-linux-gnu` 编译器的交叉编译镜像：

```bash
# 1. 构建交叉编译底座镜像 (一次性构建)
docker build -t cycore-build-cross \
  -f .agents/skills/cpp_algorithm_ops/Dockerfile.build_cross \
  .agents/skills/cpp_algorithm_ops
```

然后挂载当前工作目录，直接指定 `aarch64-linux-gnu` 编译器，在本地开发机上秒级编译出 ARM64 格式的目标动态库：

```bash
# 2. 调用交叉编译器一键编译出 ARM64 插件
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd):/workspace" \
  -w /workspace/cpp \
  cycore-build-cross \
  bash -lc "cmake -B build_cross -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc && cmake --build build_cross -j\$(nproc)"
```
编译产出为 `cpp/build_cross/lib/` 下的各类 `.so` 插件，可直接分发至远端物理机使用。

---

## 🛑 3. 跨架构 dlopen 加载失败欺骗性报错红线

* **欺骗性报错现象**：
  在远端 ARM64 物理机容器中，流图拉起加载插件时报错：`cannot open shared object file: No such file or directory`。但登录系统检查，发现该路径下的 `.so` 确实物理存在。
* **本质病因**：
  **100% 为 CPU 架构不匹配**（即在 ARM64 主机上尝试去 dlopen 加载 x86_64 格式的二进制动态库）。Linux 动态链接器检测到 ELF 文件头不匹配，无法载入该文件，会误报“文件不存在”以做诱骗。
* **解决红线要求**：
  遇到此报错时，**禁止修改目录权限或路径映射**。必须立刻在本地使用上述“第 2 节：跨平台交叉编译”指令，重新生成 ARM64 镜像插件再执行部署。
