# Sidecar 测试工具

本目录只保留两套测试链路：Dual-Leg 级联验收，以及绕过 Worker SDK 的 UCX
Transport 诊断。旧的两节点 E2E/P2P 工具已经移除。

## 本地三级级联

`compose.cascade.yaml` 在单机验证完整拓扑：

```text
worker A -> sidecar A -> sidecar B -> worker B -> sidecar B -> sidecar C -> worker C
```

正确性测试会校验帧序号、数据形状和全部 IQ 样本：

```bash
workspace/sidecar/tools/run_cascade_test.sh correctness
```

默认 Payload 为 4 KiB、64 KiB、1 MiB，B/C 的 `corrupted`、`missing`、
`duplicate`、`reordered` 必须全部为 0。

```bash
CORRECTNESS_PAYLOADS="4096 65536" FRAMES=100000 SEED=324508639 \
  workspace/sidecar/tools/run_cascade_test.sh correctness
```

吞吐测试默认覆盖 4 KiB、64 KiB、256 KiB、1 MiB，每档重复 3 次：

```bash
WARMUP_SECONDS=5 DURATION_SECONDS=60 REPETITIONS=3 \
  workspace/sidecar/tools/run_cascade_test.sh benchmark
```

使用已构建镜像时设置 `SKIP_BUILD=1`、`SIDECAR_IMAGE` 和 `CASCADE_IMAGE`。

## UCX Transport 诊断

`network_benchmark.cpp` 是不经过 Worker SDK 的外部 UCX peer，用于隔离链路抖动、
Credit 流控和 RDMA 数据路径问题：

```bash
cmake -S workspace/sidecar -B build/sidecar -DCMAKE_BUILD_TYPE=Release
cmake --build build/sidecar --target sidecar-network-benchmark --parallel

DURATION_SECONDS=30 docker-compose \
  -f workspace/sidecar/tools/compose.network-benchmark.yaml up --build

build/sidecar/network/sidecar-network-benchmark \
  --host 127.0.0.1 --port 13337 --role consumer \
  --duration 24 --profile jitter
```

TCP 只验证功能。RDMA 诊断需叠加 `compose.network-benchmark.rdma.yaml`，外部 peer
同时增加 `--strict-rdma`。

三主机 ARM64/RDMA 部署和验收流程见
[DISTRIBUTED_CASCADE_TEST.md](DISTRIBUTED_CASCADE_TEST.md)。
