# Sidecar 三主机 ARM64/RDMA 级联测试

本文定义 Dual-Leg Sidecar 的生产验收流程。构建机生成并推送 ARM64 镜像，A/B/C
测试机只拉取固定 digest，不保存源码、不现场构建。

```text
Host A: cascade-worker(source) -> Sidecar A Downstream
                                      |
                                      v UCX/RC
Host B: Sidecar B Upstream -> cascade-worker(operator) -> Sidecar B Downstream
                                                                  |
                                                                  v UCX/RC
Host C: Sidecar C Upstream -> cascade-worker(sink)
```

正确性模式校验帧序号、数据形状和全部 IQ 样本。系统采用实时流语义，不增加应用层
重传、历史帧恢复或断线注入。

## 1. 验收门禁

- 三台主机使用相同的 Sidecar digest 和 cascade-worker digest。
- `strict-rdma` 启动失败即判定失败，不允许回退 TCP 后记录为 RDMA 结果。
- A-B、B-C 的 RDMA 基线通过后，才能运行完整级联。
- 正确性错误必须为 0，之后才能进行吞吐测试。
- 测试前冻结镜像、配置、Payload、Seed、Slot、UCX 和 NUMA 参数。
- 跨主机单向延迟只有在 PTP/chrony 已锁定时才可验收。

## 2. 构建并发布 ARM64 镜像

在仓库根目录执行：

```bash
cd /home/zikun/code/common/uestcradar
git status --short
export REGISTRY=registry.chengyistudio.com/cxx
export GIT_SHA="$(git rev-parse --short=12 HEAD)"
export RELEASE="dual-leg-${GIT_SHA}-arm64"
export PLATFORM=linux/arm64
export BUILD_BASE="${REGISTRY}/sidecar:build-base@sha256:<build-base-digest>"
export RUNTIME_BASE="${REGISTRY}/sidecar:runtime-base@sha256:<runtime-base-digest>"
export SIDECAR_TAG="${REGISTRY}/sidecar:${RELEASE}"
export CASCADE_TAG="${REGISTRY}/cascade-worker:${RELEASE}"
```

发布前门禁：

```bash
cmake -S workspace/sidecar -B build/sidecar \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/sidecar --parallel
ctest --test-dir build/sidecar --output-on-failure

cmake -S workspace/sdk -B build/sdk \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/sdk --parallel
ctest --test-dir build/sdk --output-on-failure
```

构建两个制品：

```bash
docker buildx build --platform "$PLATFORM" --target runtime \
  --build-arg BUILD_BASE="$BUILD_BASE" \
  --build-arg RUNTIME_BASE="$RUNTIME_BASE" \
  -t "$SIDECAR_TAG" --load -f workspace/sidecar/Dockerfile .

docker buildx build --platform "$PLATFORM" --target cascade-worker \
  --build-arg BUILD_BASE="$BUILD_BASE" \
  --build-arg RUNTIME_BASE="$RUNTIME_BASE" \
  -t "$CASCADE_TAG" --load -f workspace/sidecar/Dockerfile .

docker image inspect "$SIDECAR_TAG" "$CASCADE_TAG" \
  --format '{{index .RepoTags 0}} arch={{.Architecture}} size={{.Size}}'
```

先运行本地正确性门禁，再由操作者推送：

```bash
SKIP_BUILD=1 SIDECAR_IMAGE="$SIDECAR_TAG" CASCADE_IMAGE="$CASCADE_TAG" \
  FRAMES=10000 workspace/sidecar/tools/run_cascade_test.sh correctness

docker login registry.chengyistudio.com
docker push "$SIDECAR_TAG"
docker push "$CASCADE_TAG"
```

推送后记录两个 `repository@sha256:...`，服务器部署只使用 digest。

## 3. 三机配置

部署包只需要：

```text
compose.cascade.distributed.yaml
node-a.env
node-b.env
node-c.env
```

三份 env 均包含以下公共配置：

### 📄 1. node-a.env（机器 A：Source 纯发包节点）

```dotenv
  # 填入刚获取的真实不可变 Digest
    SIDECAR_IMAGE=registry.chengyistudio.com/cxx/sidecar:latest
    CASCADE_IMAGE=registry.chengyistudio.com/cxx/cascade-worker:latest
  
    NODE_ID=node-a
    CASCADE_ROLE=source
    TYPE_ID=1
    TYPE_VERSION=2
  
    # 双 Leg 角色配置：禁用上游，下游连接机器 B
    UPSTREAM_ROLE=disabled
    DOWNSTREAM_ROLE=connect
    DOWNSTREAM_PEER_HOST=192.170.2.64     # ⚠️ 替换为机器 B 的真实 RDMA/物理 IP
    DOWNSTREAM_PORT=13337
  
    # 网络与 RDMA 配置
	  UCX_NET_DEVICES=hns_1:1,enp125s0f1
  	SIDECAR_DATA_PATH=strict-rdma
	UCX_TLS=rc_verbs,tcp
  	UCX_RC_VERBS_TX_MIN_INLINE=0
  	UCX_RC_VERBS_TX_MIN_SGE=2
	UCX_RNDV_THRESH=64
	UCX_ZCOPY_THRESH=64
	UCX_RNDV_SCHEME=put_zcopy
  	UCX_SOCKADDR_TLS_PRIORITY=tcp
    UCX_PROTO_INFO=y
    UCX_LOG_LEVEL=info

    # 共享内存配置
    SLOT_COUNT=64
    MAX_PAYLOAD_BYTES=1048576
    SIDECAR_SHM_SIZE=256m
```

### 📄 2. node-b.env（机器 B：Operator 算子中转节点）

```
 # 不可变镜像 Digest
    SIDECAR_IMAGE=registry.chengyistudio.com/cxx/sidecar:latest
    CASCADE_IMAGE=registry.chengyistudio.com/cxx/cascade-worker:latest
  
    NODE_ID=node-b
    CASCADE_ROLE=operator
    TYPE_ID=1
    TYPE_VERSION=2
  
    # 通道角色：上游监听机器 B 本身，下游连接机器 C (192.170.2.128)
    UPSTREAM_ROLE=listen
    UPSTREAM_BIND_HOST=192.170.2.64
    UPSTREAM_PORT=13337
    DOWNSTREAM_ROLE=connect
    DOWNSTREAM_PEER_HOST=192.170.2.80
    DOWNSTREAM_PORT=13337

    # 网络配置
	  UCX_NET_DEVICES=hns_1:1,enp125s0f1
  	SIDECAR_DATA_PATH=strict-rdma
	UCX_TLS=rc_verbs,tcp
  	UCX_RC_VERBS_TX_MIN_INLINE=0
  	UCX_RC_VERBS_TX_MIN_SGE=2
	UCX_RNDV_THRESH=64
	UCX_ZCOPY_THRESH=64
	UCX_RNDV_SCHEME=put_zcopy
  	UCX_SOCKADDR_TLS_PRIORITY=tcp
    UCX_PROTO_INFO=y
    UCX_LOG_LEVEL=info

    # 共享内存配置
    SLOT_COUNT=64
    MAX_PAYLOAD_BYTES=1048576
    SIDECAR_SHM_SIZE=256m
```

### 📄 3. node-c.env（机器 C：Sink 接收测速节点）

```
  	SIDECAR_IMAGE=registry.chengyistudio.com/cxx/sidecar:latest
    CASCADE_IMAGE=registry.chengyistudio.com/cxx/cascade-worker:latest

    NODE_ID=node-c
    CASCADE_ROLE=sink
    TYPE_ID=1
    TYPE_VERSION=2

    # 双 Leg 角色配置
    UPSTREAM_ROLE=listen
    UPSTREAM_BIND_HOST=192.170.2.80
    UPSTREAM_PORT=13337
    DOWNSTREAM_ROLE=disabled

    # 网络配置
	  UCX_NET_DEVICES=hns_1:1,enp125s0f1
  	SIDECAR_DATA_PATH=strict-rdma
	UCX_TLS=rc_verbs,tcp
  	UCX_RC_VERBS_TX_MIN_INLINE=0
  	UCX_RC_VERBS_TX_MIN_SGE=2
	UCX_RNDV_THRESH=64
	UCX_ZCOPY_THRESH=64
	UCX_RNDV_SCHEME=put_zcopy
  	UCX_SOCKADDR_TLS_PRIORITY=tcp
    UCX_PROTO_INFO=y
    UCX_LOG_LEVEL=info

    # 共享内存配置
    SLOT_COUNT=64
    MAX_PAYLOAD_BYTES=1048576
    SIDECAR_SHM_SIZE=256m
```

节点差异配置如下，示例 RDMA IP 为 A=`10.10.0.11`、B=`10.10.0.12`、
C=`10.10.0.13`：

| 文件           | 必需配置                                                                                                                                                                                                                         |
| -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `node-a.env` | `NODE_ID=node-a`、`CASCADE_ROLE=source`、`UPSTREAM_ROLE=disabled`、`DOWNSTREAM_ROLE=connect`、`DOWNSTREAM_PEER_HOST=10.10.0.12`、`DOWNSTREAM_PORT=13337`                                                             |
| `node-b.env` | `NODE_ID=node-b`、`CASCADE_ROLE=operator`、`UPSTREAM_ROLE=listen`、`UPSTREAM_BIND_HOST=10.10.0.12`、`UPSTREAM_PORT=13337`、`DOWNSTREAM_ROLE=connect`、`DOWNSTREAM_PEER_HOST=10.10.0.13`、`DOWNSTREAM_PORT=13337` |
| `node-c.env` | `NODE_ID=node-c`、`CASCADE_ROLE=sink`、`UPSTREAM_ROLE=listen`、`UPSTREAM_BIND_HOST=10.10.0.13`、`UPSTREAM_PORT=13337`、`DOWNSTREAM_ROLE=disabled`                                                                    |

将公共配置和对应节点差异配置合并到各自 env 文件。

## 4. 上线前检查

三台机器分别保存以下输出：

```bash
uname -m
lscpu
numactl --hardware
ip -br address
rdma link show
ibv_devinfo
ucx_info -d
ulimit -l
docker version
docker-compose version
```

必须确认：

- CPU 为 ARM64，镜像 inspect 显示 `arm64`。
- `/dev/infiniband` 存在，memlock 为 unlimited。
- RDMA IP、MTU、GID、PFC/ECN 或 IB Subnet Manager 配置正确。
- B/C 的 13337 端口未占用。
- 三机时钟、CPU governor、NUMA 和 IRQ 绑定已冻结。

在完整级联前，先任选一台 HNS 节点验证 Sidecar 容器能够创建 RC transport：

```bash
export SIDECAR_IMAGE='registry.chengyistudio.com/cxx/sidecar@sha256:<digest>'
docker pull "$SIDECAR_IMAGE"

docker run --rm --network host \
  --device=/dev/infiniband:/dev/infiniband \
  --ulimit memlock=-1:-1 \
  -e UCX_TLS=rc_verbs \
  -e UCX_NET_DEVICES=hns_1:1 \
  -e UCX_RC_VERBS_TX_MIN_INLINE=0 \
  -e UCX_RC_VERBS_TX_MIN_SGE=2 \
  -e UCX_UD_VERBS_TX_MIN_SGE=1 \
  --entrypoint ucx_info "$SIDECAR_IMAGE" -d
```

必须识别 `hns_1:1` 的 RC transport，且不得出现 `hns_roce_set_rq_size`、
`failed to create RC QP` 或 `ucp_worker_create`。失败时停止，不允许用 TCP 结果
替代 RDMA 准入。

每台主机验证配置并预拉镜像：

```bash
export COMPOSE=./compose.cascade.distributed.yaml
export ENV_FILE=./node-c.env       # B/C 替换为对应文件
export PROJECT=uestcradar-cascade-c # B/C 使用独立名称

docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" config --quiet
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" pull
```

## 5. 启动级联

Sidecar 按 C、B、A 顺序先于 Worker 启动：

```bash
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" up -d --no-build sidecar-node
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" logs --no-color sidecar-node
```

建链门禁：A 出现 Downstream connected；B 同时出现 Upstream/Downstream
connected；C 出现 Upstream connected。日志必须证明数据路径选择 RC 和
rendezvous/get_zcopy，不能只依据 `UCX_TLS=rc` 判断。

## 6. 正确性测试

依次测试 4096、65536、1048576 字节，每档 10000 帧。三台机器设置相同参数：

```bash
export TEST_MODE=correctness
export PAYLOAD_BYTES=65536
export FRAMES=10000
export SEED=324508639
```

Worker 按 C、B、A 顺序启动：

```bash
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" up -d --force-recreate --no-deps \
  --no-build worker-node

WORKER_ID="$(docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" ps -aq worker-node)"
docker wait "$WORKER_ID"
docker logs "$WORKER_ID"
```

硬门禁：A/B/C 退出码均为 0；帧数等于 `FRAMES`；B/C 的 `corrupted`、
`missing`、`duplicate`、`reordered` 全部为 0；Sidecar 无异常退出。

## 7. 吞吐测试

建议矩阵：4 KiB、64 KiB、256 KiB、1 MiB；预热 10 秒、测量 120 秒、重复 5
次。三台机器每轮设置相同参数：

```bash
export TEST_MODE=benchmark
export PAYLOAD_BYTES=1048576
export WARMUP_SECONDS=10
export DURATION_SECONDS=120
export RATE_MIB_S=0
```

仍按 C、B、A 启动 Worker。保存三端 Worker JSON、Sidecar 日志、`docker stats`、
容器 inspect、测试前后网卡计数器和 PTP 状态。主要结果采用 Sink C 的 `mib_s`
与 `messages_s`。

当前每条 Leg 为单 Credit 窗口。报告必须同时列出两段 `-O 1` 基线、`-O 64`
硬件基线、完整级联吞吐及其效率比，不能把单窗口结果直接等同于硬件上限。

## 8. 清理与回滚

先停止 Worker，再停止 Sidecar：

```bash
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" stop worker-node
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" down --remove-orphans
```

回滚时三台机器同时恢复上一组 Sidecar 和 cascade-worker digest。Registry 凭据
不得写入 env 或测试报告。

---

# 第二章：三主机 UCX TCP 级联测试

当 RDMA userspace provider 尚未就绪时，使用 UCX TCP 验证 Dual-Leg 拓扑、共享
内存、Credit 背压和逐帧正确性。TCP 测试不能作为 RDMA、DMA 直连或网络零拷贝
的验收结果。

## 2.1 创建独立 TCP 配置

不要覆盖第一章的 RDMA env 文件。在三台机器各自复制一份：

```bash
# A
cp node-a.env node-a-tcp.env

# B
cp node-b.env node-b-tcp.env

# C
cp node-c.env node-c-tcp.env
```

同一条链路两端必须统一使用 TCP，不能混用 `rc` 和 `tcp`。TCP 模式下
`UCX_NET_DEVICES` 必须填写 Linux netdev 名称，不能填写 `mlx5_0:1` 或
`dpuRdma7s0f0:1` 等 RDMA 设备名。

先确认到下一跳的路由网卡：

```bash
# A 上执行，记录输出中的 dev 字段
ip route get 192.170.2.64

# B 上执行，预期经过 enp2s0f1
ip route get 192.170.2.128

# C 上确认监听地址
ip -4 addr show dev ens1
```

将三份 TCP env 的网络部分分别设置为：

```dotenv
# node-a-tcp.env
UPSTREAM_ROLE=disabled
DOWNSTREAM_ROLE=connect
DOWNSTREAM_PEER_HOST=192.170.2.64
DOWNSTREAM_PORT=13337

UCX_NET_DEVICES=<A上ip-route输出的网卡名>
SIDECAR_DATA_PATH=functional
UCX_TLS=tcp,self
UCX_SOCKADDR_TLS_PRIORITY=tcp
UCX_PROTO_INFO=y
UCX_LOG_LEVEL=info
```

```dotenv
# node-b-tcp.env
UPSTREAM_ROLE=listen
UPSTREAM_BIND_HOST=192.170.2.64
UPSTREAM_PORT=13337
DOWNSTREAM_ROLE=connect
DOWNSTREAM_PEER_HOST=192.170.2.128
DOWNSTREAM_PORT=13337

UCX_NET_DEVICES=enp2s0f1
SIDECAR_DATA_PATH=functional
UCX_TLS=tcp,self
UCX_SOCKADDR_TLS_PRIORITY=tcp
UCX_PROTO_INFO=y
UCX_LOG_LEVEL=info
```

```dotenv
# node-c-tcp.env
UPSTREAM_ROLE=listen
UPSTREAM_BIND_HOST=192.170.2.128
UPSTREAM_PORT=13337
DOWNSTREAM_ROLE=disabled

UCX_NET_DEVICES=ens1
SIDECAR_DATA_PATH=functional
UCX_TLS=tcp,self
UCX_SOCKADDR_TLS_PRIORITY=tcp
UCX_PROTO_INFO=y
UCX_LOG_LEVEL=info
```

Node C 的实际地址是 `192.170.2.128`，不能写成 `192.162.2.128`。三份 TCP env
继续使用第一章中相同的不可变 Sidecar 和 cascade-worker digest。

## 2.2 检查配置并拉取镜像

三台机器分别设置自己的参数：

```bash
# A
export COMPOSE=./compose.cascade.distributed.yaml
export ENV_FILE=./node-a-tcp.env
export PROJECT=uestcradar-cascade-tcp-a

# B 使用 node-b-tcp.env / uestcradar-cascade-tcp-b
# C 使用 node-c-tcp.env / uestcradar-cascade-tcp-c
```

然后分别执行：

```bash
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" config |
  grep -E 'image:|UCX_TLS|UCX_NET_DEVICES|DATA_PATH|BIND_HOST|PEER_HOST'

docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" pull
```

渲染结果中不能出现 `strict-rdma`、`UCX_TLS=rc`、`mlx5_0:1` 或
`dpuRdma7s0f0:1`。

## 2.3 按 C、B、A 启动 Sidecar

先在 C，再在 B，最后在 A 执行：

```bash
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" down

docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" up -d --no-build sidecar-node

docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" logs --no-color sidecar-node
```

连接门禁：

- A 出现 `leg=downstream event=connected`。
- B 同时出现 upstream 和 downstream 的 `event=connected`。
- C 出现 `leg=upstream event=connected`。
- 日志中不再出现 `transport 'rc' is not available` 或
  `ucp_init: No such device`。

## 2.4 按 C、B、A 运行正确性测试

三台机器设置完全相同的测试参数：

```bash
export TEST_MODE=correctness
export PAYLOAD_BYTES=65536
export FRAMES=10000
export SEED=324508639
```

先在 C，再在 B，最后在 A 启动 Worker：

```bash
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" up -d --force-recreate --no-deps \
  --no-build worker-node

WORKER_ID="$(docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" ps -aq worker-node)"
docker wait "$WORKER_ID"
docker logs "$WORKER_ID"
```

验收要求：

- A/B/C Worker 退出码全部为 0。
- 三端 `frames=10000`。
- B/C 的 `corrupted`、`missing`、`duplicate`、`reordered` 全部为 0。
- Sidecar 没有异常退出或持续重试。

64 KiB 通过后，以相同步骤测试 4096 和 1048576 字节 Payload。

## 2.5 TCP 吞吐测试

正确性通过后，三台机器设置相同参数：

```bash
export TEST_MODE=benchmark
export PAYLOAD_BYTES=1048576
export WARMUP_SECONDS=10
export DURATION_SECONDS=120
export RATE_MIB_S=0
```

仍按 C、B、A 启动 Worker。主要结果采用 Sink C 的 `mib_s` 和 `messages_s`，并
保存三端 Worker JSON、Sidecar 日志和 `docker stats`。该结果只能标记为 UCX
TCP 基线。

## 2.6 清理 TCP 测试

三台机器分别执行：

```bash
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" stop worker-node
docker-compose --env-file "$ENV_FILE" -p "$PROJECT" \
  -f "$COMPOSE" down --remove-orphans
```
