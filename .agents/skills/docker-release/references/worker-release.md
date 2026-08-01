# Worker 发布

Worker 开发者可以自由选择构建工具、目录和 Entrypoint。正式发布 Dockerfile 必须位于
`workspace/examples/<worker-name>/`；当前唯一权威入口为：

```text
workspace/examples/cascade_worker/Dockerfile
```

基础镜像保持为：

```text
registry.chengyistudio.com/cxx/algo-base:latest
```

发布 Tags：

```text
registry.chengyistudio.com/cxx/worker:cascade-sha-<gitsha12>-arm64
registry.chengyistudio.com/cxx/worker:cascade-latest
```

发布命令：

```bash
./.agents/skills/docker-release/scripts/release.sh \
  --remote-dir /root/workspace/uestcradar \
  --component worker
```

禁止使用 `workspace/sidecar/Dockerfile --target cascade-worker` 发布正式 Worker。
