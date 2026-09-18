# SDK / Algo Base 发布

SDK 的正式容器交付物是 Worker 编译与运行基座：

```text
registry.chengyistudio.com/cxx/algo-base:sha-<gitsha12>-arm64
registry.chengyistudio.com/cxx/algo-base:latest
```

构建入口为 `workspace/sdk/Dockerfile` 的 `algo-base` target，构建上下文必须是仓库根目录，
因为 SDK 与 `workspace/common/` 共同定义 RingBuffer ABI。

必需契约：

```text
io.uestcradar.contract=algo-base/v2
OS/Architecture=linux/arm64
/usr/local/lib/libuestcradar_sdk.so
/usr/local/include/sdk.h
/usr/local/include/data.h
/usr/local/lib/cmake/cycomm_sdk/cycomm_sdkConfig.cmake
/usr/local/share/cycomm_sdk/contracts/contracts.manifest.json
```

统一使用远程发布入口：

```bash
./.agents/skills/docker-release/scripts/release.sh \
  --remote-dir /root/workspace/uestcradar
```

在交互菜单中选择 `SDK / Algo Base`。脚本在 ARM64 发布机上执行以下闭环：

1. 拉取 `SDK_BASE_IMAGE`，默认 `registry.chengyistudio.com/cxx/ubuntu:24.04`。
2. 构建并验证不可变 `algo-base:sha-<gitsha12>-arm64`。
3. 推送不可变 Tag、拉回重新验证，再更新 `algo-base:latest`。
4. 不自动构建 Worker；之后应使用新的 `ALGO_BASE` 逐个发布 Worker。

SDK 发布失败时不得更新 `algo-base:latest`。不可变 Tag 已存在时脚本直接退出，不覆盖。
