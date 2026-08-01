# Sidecar 发布

正式构建入口为 `workspace/sidecar/Dockerfile` 的 `runtime` target。

```text
registry.chengyistudio.com/cxx/sidecar:sha-<gitsha12>-arm64
registry.chengyistudio.com/cxx/sidecar:latest
```

必需契约：

```text
io.uestcradar.contract=sidecar/v1
Entrypoint=/app/sidecar
OS/Architecture=linux/arm64
```

统一使用远程发布入口：

```bash
./.agents/skills/docker-release/scripts/release.sh \
  --remote-dir /root/workspace/uestcradar \
  --component sidecar
```
