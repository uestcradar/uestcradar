# 编译指南 (Compilation Guide)

为了确保算法插件在不同运行环境中的兼容性，我们统一在 Docker 镜像中执行编译。

---

## 1. 本地同构编译 (X86 开发机 -> X86 容器)

适用于在本地 X86 容器（如本地 `node8`）中进行调试和运行：

```bash
# 1. 自动获取当前 ci/Dockerfile.build 对应的哈希 Tag
DOCKERFILE_HASH=$(sha256sum ci/Dockerfile.build | cut -d' ' -f1 | cut -c1-12)
CPP_IMAGE="cycore-build:${DOCKERFILE_HASH}"

# 2. 挂载工作区，在 Docker 内部一键构建本地 X86 格式的插件
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd):/workspace" \
  -w /workspace/draft \
  "${CPP_IMAGE}" \
  bash -lc "cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
```
编译产出为 `draft/build/my_plugin.so`。

---

## 2. 跨平台交叉编译 (X86 开发机 -> ARM64 二进制)

适用于将插件推送到 ARM64 物理机（如 `node2`、`node4`）运行环境。

在项目根目录下，首先基于 Skill 预置的 Dockerfile 构建出包含 `aarch64-linux-gnu` 编译器的极简交叉编译镜像：

```bash
# 1. 构建小巧的交叉编译底座
docker build -t cycore-build-cross \
  -f .agents/skills/develop_new_algorithm/Dockerfile.build_cross \
  .agents/skills/develop_new_algorithm
```

然后挂载当前工作目录，直接指定 `aarch64-linux-gnu` 编译器，在本地秒级编译出 ARM64 格式的目标动态库：

```bash
# 2. 调用交叉编译器一键编译
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd):/workspace" \
  -w /workspace/draft \
  cycore-build-cross \
  bash -lc "rm -rf build_cross && cmake -B build_cross -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc && cmake --build build_cross -j\$(nproc)"
```
编译产出为 `draft/build/my_plugin.so`，可直接部署到异地物理机容器中。

---

## 3. 跨架构 dlopen 加载失败欺骗性报错红线

* **欺骗性现象**：
  流图拉起加载插件时报错：`cannot open shared object file: No such file or directory`。但该路径下确实存在对应的 `.so` 文件。
* **本质病因**：
  **100% 为 CPU 架构不匹配**（例如在 ARM64 主机上加载了 x86_64 二进制库）。Linux 动态链接器因格式头不匹配无法识别，会误报为文件不存在。
* **解决要求**：
  遇到该错误时，禁止盲目修改目录挂载或权限。必须在本地执行上述“第 2 节：跨平台交叉编译流程”重新编译为 ARM64 格式插件再部署。
