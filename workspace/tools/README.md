# 雷达 Sidecar 工具集与级联部署指南 (`workspace/tools`)

本文档说明 `workspace/tools` 文件夹的作用，以及如何在单机环境与多物理节点分布式环境下，基于预构建镜像快速拉起 Sidecar 级联拓扑链与自动化运维。

---

## 📂 `workspace/tools` 文件夹作用

`workspace/tools` 目录集中存放雷达 Sidecar 级联系统的**本地调试配置、分布式部署模板与自动化集群运维工具脚本**。

通过将部署配置与运维工具统一收拢到该目录下，可以实现：
1. **业务代码与部署工具解耦**：方便开发人员与运维人员进行一键调试与部署。
2. **标准化集群管理**：提供集群级一键分发脚本，保证多物理节点间的配置一致性。

### 📦 核心工具与文件清单

| 文件名 | 作用与特点 |
| :--- | :--- |
| **`workspace/tools/compose.yaml`** | **单机 7 服务全拓扑配置**：在单台机器上以 Docker 容器方式拉起 `sidecar-a/b/c` + `worker-a/b/c` + `telemetry-web` 完整 7 服务级联拓扑与监控界面。 |
| **`workspace/tools/compose.cascade.distributed.yaml`** | **多物理节点分布式拓扑**：跨 3 台物理服务器部署 `sidecar-node` 与 `worker-node` 的标准化通用模板。 |
| **`workspace/tools/deploy_cli.py`** | **分布式智能推导部署 CLI**：自动推导 3 节点拓扑连线、自动剔除冗余变量、支持交互/命令行输入与一键远程分发拉起 (`--up`)。 |
| **`workspace/tools/node.env`** | **分布式环境变量模板**：包含完备的镜像 Tag、节点身份、Web 遥测 (8081/9900)、RDMA 网络与共享内存的标准化环境配置模板。 |
| **`workspace/tools/sync_distributed_compose.sh`** | **集群一键分发脚本**：一键将 `compose.cascade.distributed.yaml` 同步推送覆盖到所有集群节点的 `/root/workspace/docker/` 目录下。 |
| **`workspace/tools/README.md`** | **部署与运维使用说明文档**。 |

---

## 🔄 如何一键更新集群所有节点上的 `compose.cascade.distributed.yaml`

当您在本地修改了 `workspace/tools/compose.cascade.distributed.yaml`（例如修改了环境变量、UCX 参数或镜像 Tag）后，只需在项目根目录下运行一行命令，即可完成集群所有物理节点的自动同步更新：

### 1. 执行一键同步更新脚本

在项目根目录下运行：

```bash
./workspace/tools/sync_distributed_compose.sh
```

### 2. 脚本工作原理与输出

* **目标路径**：脚本会自动连接集群所有物理节点（`192.162.2.16` ~ `192.162.2.192`），建立 `/root/workspace/docker/` 目录并推送覆写 `compose.cascade.distributed.yaml`。
* **认证机制**：默认使用 SSH 密钥或密码（`SSH_USER=root`, `SSH_PASSWORD=111111`）自动登录分发。
* **输出示例**：

```text
==> 开始一键分发 workspace/tools/compose.cascade.distributed.yaml 到所有集群节点的 /root/workspace/docker/compose.cascade.distributed.yaml
    SSH 用户: root
    目标节点数: 9

==> 同步至节点 [192.162.2.16]... ✔ 成功
==> 同步至节点 [192.162.2.32]... ✔ 成功
...
=========================================================
 集群同步完成汇总报告
=========================================================
 成功节点数 (9): 192.162.2.16 192.162.2.32 ...
 失败节点数 (0): 无
=========================================================
```

---

## 🚀 场景一：在单机上启动多 Docker 级联链路（单机 7 服务）

在项目根目录下，直接使用私有源镜像秒级启动：

### 1. 从私有仓库预拉取最新镜像
```bash
export REGISTRY="registry.chengyistudio.com/cxx"

docker compose -f workspace/tools/compose.yaml pull
```

### 2. 启动 7 服务级联拓扑
```bash
docker compose -f workspace/tools/compose.yaml up -d
```

### 3. 打开 Web 监控面板
在浏览器访问：**`http://localhost:8081`**（或 `http://<服务器IP>:8081`）。
即可看到 `node-a ➔ node-b ➔ node-c` 动态链路拓扑图与实时 RingBuffer 水位数据！

### 4. 停止与清理
```bash
docker compose -f workspace/tools/compose.yaml down
```

---

## ⚡ 场景二：在多物理节点上启动分布式级联链路

在 3 台独立物理服务器（Host A / Host B / Host C）组成的分布式网络中部署：

### 1. 一键分发最新的分布式 Compose 模板至集群
```bash
./workspace/tools/sync_distributed_compose.sh
```

### 2. 在 Host C 物理机上独立拉起 Web 遥测面板
在 Host C（Web 观察机）上运行一次 `docker run`：
```bash
docker run -d \
  --name telemetry-web \
  --network host \
  --read-only \
  --cap-drop=ALL \
  --security-opt no-new-privileges:true \
  --restart unless-stopped \
  registry.chengyistudio.com/cxx/telemetry-web:latest
```

### 3. 在 3 台物理节点上配置环境变量文件并启动

在各物理节点的 `/root/workspace/docker/` 目录下放置各自的 `.env.local` 环境变量配置文件后启动：

* **Host C（第 1 步启动）**：
  ```bash
  cd /root/workspace/docker
  docker compose -f compose.cascade.distributed.yaml --env-file node-c.env up -d
  ```
* **Host B（第 2 步启动）**：
  ```bash
  cd /root/workspace/docker
  docker compose -f compose.cascade.distributed.yaml --env-file node-b.env up -d
  ```
* **Host A（第 3 步启动）**：
  ```bash
  cd /root/workspace/docker
  docker compose -f compose.cascade.distributed.yaml --env-file node-a.env up -d
  ```

### 4. 打开全局监控
打开 Host C 的 Web 监控地址 **`http://192.168.1.103:8081`** 即可查看跨物理机级联拓扑！
