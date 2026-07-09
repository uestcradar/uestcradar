# 部署指南 (Deployment Guide)

本指南介绍如何将本地（X86 开发机）编译好的算法动态库插件（如 `libalgorithm_block.so`），安全地分发并部署至远端目标主机（如 192.162.2.64）的 DU 运行容器（如 `sim-du-node4` 或 `sim-du-node8`）中进行加载运行。

---

## 1. 传输插件至远端目标主机

使用 `scp` 将本地编译完成的 ARM64 动态库（通常位于 `build_arm64/` 中），传输至远端服务器 `192.162.2.64` 的临时缓存目录：

```bash
# 本地 X86 宿主机上运行：拷贝到 64 宿主机的 /root 目录下 (默认密码: 111111)
scp -o StrictHostKeyChecking=no \
  .agents/skills/develop_new_algorithm/algorithm_template/build_arm64/libalgorithm_block.so \
  root@192.162.2.64:/root/
```

---

## 2. 拷贝入运行中的 Docker 容器

登录 64 宿主机，通过 **`docker cp`** 命令，将暂存在 `/root/` 的插件动态库直接热复制注入到运行的目标 DU 容器 `sim-du-node4`（或 `sim-du-node8`）的流图算子插件目录下：

```bash
# 1. 确保目标容器的插件目录存在
sshpass -p "111111" ssh -o StrictHostKeyChecking=no root@192.162.2.64 "docker exec sim-du-node4 mkdir -p /workspace/lib/du/flowgraph/plugins/"

# 2. 将 so 文件热复制进容器内部
sshpass -p "111111" ssh -o StrictHostKeyChecking=no root@192.162.2.64 "docker cp /root/libalgorithm_block.so sim-du-node4:/workspace/lib/du/flowgraph/plugins/"

# 3. 授权容器内 so 的可执行权限
sshpass -p "111111" ssh -o StrictHostKeyChecking=no root@192.162.2.64 "docker exec sim-du-node4 chmod +x /workspace/lib/du/flowgraph/plugins/libalgorithm_block.so"
```

---

## 3. 宿主机快捷挂载部署方案 (推荐)

如果目标容器 `sim-du-node4` 在拉起时已经通过挂载卷的方式，将远端宿主机的插件物理目录暴露给了容器：
* **挂载示例**：
  `docker run -v /home/workspace/cycore/lib/du/flowgraph/plugins:/workspace/lib/du/flowgraph/plugins ...`

#### 部署命令：
此时，我们**无需执行 docker cp**。只需在本地直接用 `scp` 将编译好的 `.so` 覆盖写入远端宿主机的挂载目录，容器内部即会实时自动热加载同步：

```bash
scp -o StrictHostKeyChecking=no \
  .agents/skills/develop_new_algorithm/algorithm_template/build_arm64/libalgorithm_block.so \
  root@192.162.2.64:/home/workspace/cycore/lib/du/flowgraph/plugins/
```
流图主程序在检测到该路径下的文件变更后，会自动完成算法热插拔加载。
