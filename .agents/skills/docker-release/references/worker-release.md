# Worker 发布

Worker 开发者可以自由选择构建工具、目录和 Entrypoint。正式发布 Dockerfile 必须位于
`workspace/examples/<worker-name>/`。发布脚本从含有 Dockerfile 的直接子目录生成
交互菜单，例如：

```text
workspace/examples/cascade_worker/Dockerfile
```

基础镜像保持为：

```text
registry.chengyistudio.com/cxx/algo-base:latest
```

发布 Tags：

```text
registry.chengyistudio.com/cxx/worker:<worker-name>-sha-<gitsha12>-arm64
registry.chengyistudio.com/cxx/worker:<worker-name>-latest
```

`<worker-name>` 由目录名转为小写并将下划线转换为连字符。例如 `cascade_worker`
对应 `cascade-worker-latest`。

发布命令：

```bash
./.agents/skills/docker-release/scripts/release.sh \
  --remote-dir /root/workspace/uestcradar
```

在交互菜单中选择 `Worker`，再选择具体目录。Dockerfile 至少必须包含 `FROM` 以及
Worker v1 的四个 Labels；实际 Docker 构建和构建后的镜像契约校验任一失败都会立即退出。

禁止使用 `workspace/sidecar/Dockerfile --target cascade-worker` 发布正式 Worker。
