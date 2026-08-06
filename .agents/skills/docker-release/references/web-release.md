# Web 发布

正式构建入口为 `workspace/web/Dockerfile`。

```text
registry.chengyistudio.com/cxx/web:sha-<gitsha12>-arm64
registry.chengyistudio.com/cxx/web:latest
```

必需契约：

```text
io.uestcradar.contract=web/v1
Entrypoint=/telemetry
OS/Architecture=linux/arm64
```

Web 默认使用 `registry.chengyistudio.com/cxx/web:build-base`。该基座由
`workspace/web/docker/Dockerfile.build-base` 构建，必须包含 Go、Protobuf、Node/npm 和
与当前 lockfile 对应的离线前端依赖；发布脚本不会再从旧 telemetry-web 基座降级初始化。

```bash
./.agents/skills/docker-release/scripts/release.sh \
  --remote-dir /root/workspace/uestcradar
```

在交互菜单中选择 `Web`。
