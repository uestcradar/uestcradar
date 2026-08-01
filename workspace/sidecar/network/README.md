# UCX Network 模块

本目录是可独立构建的点对点 UCX/UCP 字节传输模块。它只处理普通内存
span、UCX endpoint、异步请求和内存注册，不依赖 RingBuffer。

## 边界

- `UCXTransport::accept_one()` 接受一个连接，`connect()` 连接一个对端。
- `send()` / `receive()` 使用 UCP tag 非阻塞接口，返回 move-only
  `UCXRequest`；调用者必须保持 buffer 有效，直到 `wait()` 完成。
- 每个实例使用 `UCS_THREAD_MODE_SINGLE`，同一个 transport 只能由一个线程驱动。
- Payload 是裸字节，模块不做序列化、分帧、路由、重试或业务解析。
- `register_memory()` 使用 `ucp_mem_map` 注册任意 CPU 内存；
  `send/receive` 可携带对应的 `UCXMemoryRegion`，并拒绝越界 span。
- tag `UINT64_MAX` 和 `UINT64_MAX - 1` 保留给内部建连握手，调用方不要使用。
- `functional` 模式强制 TCP，只用于功能验证和明确的遥测标识。
- `strict_rdma` 模式限定 RC，并强制 rendezvous/get_zcopy；它仍要求可用
  RDMA 设备、驱动、锁页权限和足够的 memlock。TCP 测试不构成 DMA
  零拷贝证明。

## 独立构建与测试

依赖 CMake、C++20、pkg-config 和 UCX 开发包：

```bash
cmake -S workspace/sidecar/network \
      -B build/sidecar-network \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=ON
cmake --build build/sidecar-network --parallel
ctest --test-dir build/sidecar-network --output-on-failure
```

产物：

- `libcycomm_network.a`
- `ucx-transport-test`
- `ucx-network-benchmark`
- `sidecar-network-benchmark`

## TCP 双容器基线

默认镜像是项目 ARM64 基础镜像。在非 ARM64 开发机上可用
`BASE_IMAGE=ubuntu:24.04` 做功能验证，但其性能数字不能作为 ARM64 目标机结论。

```bash
cd workspace/sidecar/network
BASE_IMAGE=ubuntu:24.04 \
UCX_BENCHMARK_IMAGE=uestcradar-ucx-benchmark:dev \
docker compose -f compose.benchmark.yaml \
  up --build --abort-on-container-exit --exit-code-from client
docker compose -f compose.benchmark.yaml down
```

benchmark 对 4 KiB、64 KiB、512 KiB 依次执行：

1. 数据完整性校验；
2. 16 次不计时预热；
3. window=64 的单向 streaming；
4. ping-pong RTT。

Client 输出 streaming 的 GiB/s、OPS，以及 RTT mean/p50/p99。`--quick`
只用于快速回归；去掉 compose 中两处 `--quick` 才是完整基线。

## 原生 ARM64 / RDMA profile

只有目标机具备可用 RDMA 设备、驱动及 UCX transport 时，才使用覆盖文件。
两台机器分别只启动自己的 service，并把 Client 的 `PEER_HOST` 指向 Server：

```bash
# Server
docker compose -f compose.benchmark.yaml -f compose.rdma.yaml \
  up --build server

# Client（另一台机器）
PEER_HOST=<server-ip> \
docker compose -f compose.benchmark.yaml -f compose.rdma.yaml \
  run --rm client
```

RDMA profile 使用 host network、host IPC、`/dev/infiniband`、unlimited
memlock，并默认设置 `UCX_TLS=rc,tcp,self`。可先用 `ucx_info -d` 核对实际
设备和 transport；结果报告必须注明 CPU、网卡、链路、UCX 版本和
`UCX_TLS`。

## 文件

```text
network/
├── CMakeLists.txt
├── README.md
├── ucx_transport.hpp
├── ucx_transport.cpp
├── transport_test.cpp
├── benchmark.cpp
├── compose.benchmark.yaml
└── compose.rdma.yaml
```
