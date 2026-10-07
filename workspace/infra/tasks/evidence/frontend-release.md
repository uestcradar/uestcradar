# ARM64 Harbor 发布与拉回复验

日期：2026-10-07。用户明确允许提交、推送 `upstream/feat/signalsink` 及发布后继续执行。

## Frontend

- 首次实现提交：`d021814e5d1ed56efd27c6466a36838b56439910`，已推送 upstream。
- 实际浏览器发现时钟问题：新帧 receivedAt 晚于上一秒 UI now，30 个采样中 24 个错误标为过期。修正为接收帧时同步更新 now；提交 `d06d1d28f318784f270856f920da59b4cdf94d85`，已推送。
- **采用版本**：`registry.chengyistudio.com/cxx/frontend:sha-d06d1d28f318-arm64`。
- **采用 digest**：`registry.chengyistudio.com/cxx/frontend@sha256:7784fe19bc47d1509705d347595d9e92254b59e2efb2c1b446bc63281ee28052`。
- Image ID：`sha256:2ef9656510c7c0636e568785d6588e086fe4ad1c61c990ab8bd4d4257a8f8870`，linux/arm64，入口 `/frontend`，用户 `65532:65532`。
- 最初 d021814 发布的 `7fedf452…` 保留，不覆盖；两个案例不再使用它。

构建机 `root@192.162.2.64` 为原生 ARM64。通过 `git archive` 导出上述已提交版本的 Frontend，专用目录构建，不夹带工作区中的 Compose 草稿。发布前 pull 返回明确 artifact not found，不把权限/网络错误当成不存在。

实际命令核心如下，均成功：

```bash
git archive d06d1d28f318784f270856f920da59b4cdf94d85 workspace/infra/frontend
# 将归档解开到 ARM 构建机的专用目录后：
docker build --label org.opencontainers.image.revision=d06d1d28f318784f270856f920da59b4cdf94d85 \
  -f workspace/infra/frontend/Dockerfile \
  -t registry.chengyistudio.com/cxx/frontend:sha-d06d1d28f318-arm64 .
docker push registry.chengyistudio.com/cxx/frontend:sha-d06d1d28f318-arm64
docker pull registry.chengyistudio.com/cxx/frontend:sha-d06d1d28f318-arm64
```

Dockerfile 实际执行 UI build、11 个 Vitest 测试、Go test/vet 后才构建运行镜像，见 [发布日志](frontend-release-build.txt)。本机通过 Compose 从 Harbor 拉取固定 digest；实际浏览器验证新帧显示实时，真正超过 3 秒未更新才标为非实时。没有使用 save/load、本地构建或 dev tag。

## KT3 必要的 Sink 镜像修正

旧 `rd-sink-latest@sha256:f17f04d2…` 在真实 22196×64 输出上退出 1：

```text
[rd-sink] FAIL RDFrame metadata is invalid
```

反汇编显示其元数据验证仍比较 doppler_bin_count == 65（0x1624 的立即数 0x41）；另一个缓存旧 Infra 镜像也有该限制。仓库现有 `infra/rd_verifier.hpp` 已按非零实际尺寸、payload 边界与有限值验证，不需要修改算法、测试或协议。

从同一提交导出 KT3，使用**原有且未修改**的 `docker/Dockerfile.infra`，固定原基座 digest，在 ARM 构建机运行已有 CMake/CTest，再发布全新标签：

```bash
docker build -f docker/Dockerfile.infra \
  --build-arg BUILD_BASE=registry.chengyistudio.com/cxx/algo-base@sha256:fc10bc0d656af3c2418391daa129821226a109ede59f7098e5cf8a8a7a3915fa \
  --build-arg ALGO_BASE=registry.chengyistudio.com/cxx/algo-base@sha256:d63bad75fc9cc8c4f0fe4b8bbee0e4089a6634318ee2be57692b83fd59425487 \
  --label org.opencontainers.image.revision=d06d1d28f318784f270856f920da59b4cdf94d85 \
  -t registry.chengyistudio.com/cxx/worker:rd-sink-sha-d06d1d28f318-arm64 .
docker push registry.chengyistudio.com/cxx/worker:rd-sink-sha-d06d1d28f318-arm64
```

- 已有 CTest **2/2 通过**，见 [构建发布日志](rd-sink-release-build.txt)。
- 新 digest：`registry.chengyistudio.com/cxx/worker@sha256:9549a84a6853398de409237ac3b15f704d79364d1b11b35eab96fb3456b52948`，linux/arm64。
- 未覆盖旧标签，未改公共发布工具；只更正案例的镜像引用。本机从 Harbor 拉取后恢复真实 Sink 校验，见 KT3 证据。

旧镜像不兼容与 UI 时钟缺陷均为已修正的软件问题，**不是硬件阻塞**。
