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

Web 默认使用 `registry.chengyistudio.com/cxx/web:build-base`。若该 Tag 尚不存在，
发布脚本会从现有 `cxx/telemetry-web:build-base` 初始化它；本阶段不删除旧仓库。

```bash
./.agents/skills/docker-release/scripts/release.sh \
  --remote-dir /root/workspace/uestcradar
```

在交互菜单中选择 `Web`。
