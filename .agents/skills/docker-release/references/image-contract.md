# 私有源镜像契约（Ring ABI v6）

## SDK / Algo Base

```dockerfile
LABEL io.uestcradar.contract="algo-base/v2"
```

`algo-base/v2` 安装 SDK 6.x、Ring ABI v6、C++ 头文件、CMake Package 和生成的帧契约目录。
它是构建基座，不需要 Entrypoint。

## Sidecar

```dockerfile
LABEL io.uestcradar.contract="sidecar/v2"
```

## Web

```dockerfile
LABEL io.uestcradar.contract="web/v1"
```

## Worker

第三方算法镜像只需提供以下四个 Labels：

```dockerfile
LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="operator" \
      io.uestcradar.input="1:2" \
      io.uestcradar.output="2:2"
```

- `roles` 是 `source`、`operator`、`sink` 的逗号分隔子集。
- `input`、`output` 为 `none` 或 `<type_id>:<type_version>`。
- Source 必须有 output，Operator 必须同时有 input/output，Sink 必须有 input。
- 相邻 Worker 由编排控制面校验 `upstream.output == downstream.input`。
- 当前类型：`1:2=IQFrame`、`2:2=PulseCompressionFrame`、`3:2=RDFrame`。
- `worker/v2` 与 `sidecar/v2` 固定对应 Ring ABI v6；v1 镜像不得混用。

不约束构建工具、源码目录、第三方依赖、可执行文件名称或 Entrypoint 路径。镜像只需
存在至少一个默认 Entrypoint 或 Cmd。
