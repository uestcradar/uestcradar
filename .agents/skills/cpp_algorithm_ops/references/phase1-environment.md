# Phase 1: 环境变量定义与解析

在执行任何编译与部署前，部署脚本必须首先解析出目标物理节点的拓扑与物理连接元数据。

我们已在技能根目录下预置了 **[部署环境变量模板 (env.template)](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/env.template)** 配置文件。

## 📌 环境变量字段规范定义

| 环境变量名 | 作用与说明 | 典型配置取值 |
| :--- | :--- | :--- |
| **`REMOTE_IP`** | 目标物理节点的网络 IP 地址。 | `192.162.2.32` (Node2) / `192.162.2.64` (Node4) |
| **`REMOTE_USER`** | 物理机 SSH 登录用户名。 | 默认为 `root` |
| **`REMOTE_PASS`** | 物理机 SSH 登录密码，用于自动化 `sshpass` 建立网络会话。 | `111111` |
| **`USE_DOCKER`** | 目标主机上的雷达服务是否部署运行在 Docker 容器中。 | `true` / `false` |
| **`DOCKER_NAME`** | 目标雷达服务容器名称（用于第四步的热重启）。 | `sim-du-node2` / `sim-du-node4` |
| **`CODE_DIR`** | 目标机上的 `cycore` 部署根路径（用于插件目录拷贝）。 | `/home/workspace/cycore` |
| **`ARCH`** | 目标物理机 CPU 系统架构，决定第二步所调用的编译器。 | `arm` (AArch64 板卡) / `x86` (PC 宿主机) |

