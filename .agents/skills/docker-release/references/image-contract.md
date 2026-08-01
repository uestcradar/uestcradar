# 私有源镜像契约 v1

## Sidecar

```dockerfile
LABEL io.uestcradar.contract="sidecar/v1"
```

## Web

```dockerfile
LABEL io.uestcradar.contract="web/v1"
```

## Worker

第三方算法镜像只需提供以下四个 Labels：

```dockerfile
LABEL io.uestcradar.contract="worker/v1" \
      io.uestcradar.roles="operator" \
      io.uestcradar.input="1:1" \
      io.uestcradar.output="2:1"
```

- `roles` 是 `source`、`operator`、`sink` 的逗号分隔子集。
- `input`、`output` 为 `none` 或 `<type_id>:<type_version>`。
- Source 必须有 output，Operator 必须同时有 input/output，Sink 必须有 input。
- 相邻 Worker 由编排控制面校验 `upstream.output == downstream.input`。
- 当前类型：`1:1=IQFrame`、`2:1=PulseCompressionFrame`、`3:1=RDFrame`。

不约束构建工具、源码目录、第三方依赖、可执行文件名称或 Entrypoint 路径。镜像只需
存在至少一个默认 Entrypoint 或 Cmd。
