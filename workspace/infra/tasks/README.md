# Frontend / Web 分离任务

**目标：单机直接访问和服务器经 Web 内嵌，使用同一 Frontend，看到同一套节点结果。** 抽出已有预览，不重写算法、绘图或部署管理。

F00–F12 已完成：独立应用已实现并通过 ARM64 构建、单元测试、race 检查和进程冒烟。Compose、Harbor 正式发布、真实案例与 Web 集成尚未完成。当前没有硬件阻塞；F14 等待干净源码版本与发布确认，未自动提交。

## 文档

- [plan.md](plan.md)：范围、数据去向、部署入口及必要验证。
- [todo.md](todo.md)：可执行任务；先单机，再完成必需的 Web 集成。
- [规格](../SPEC-frontend-runtime.md) / [能力图](../CAPABILITY_MAP.md)：功能与验收基线。
- [ARM64 环境证据](evidence/arm64-environment.md)：已完成操作；宿主重启后须重新检查。
- [应用执行证据](evidence/frontend-runtime.md)：实际源码、镜像、测试及未做事项。

## 保留的部署边界

KT2、KT3 各在案例根目录只保留一份 `compose.yaml`、一个 project。镜像发布、Docker ARM64 与 Harbor 访问准备好后：

```bash
cd workspace/examples/KT2  # 或 workspace/examples/KT3
docker compose up -d --no-build
```

默认 ARM64 Harbor digest、TCP、无 Web；不要求额外环境文件、override、启动包装脚本或手动 infra/worker 顺序。KT2 算法页为 `http://127.0.0.1:8082`，KT3 RD 页为 `http://127.0.0.1:8083`；两案例顺序运行。以上是目标入口，尚未实现。

**plan 第 5 节是日常部署，第 7 节仅供开发/验收。** 构建测试在 ARM 服务器进行，发布 Harbor 后本机拉取运行；不把测试工具变成部署依赖。

## 交付顺序与完成标准

1. 抽出节点页面及必要后端，复用已有预览协议、解码、绘图和测试。
2. 每个本机案例一次完成真实结果、Harbor 拉取与故障隔离验收；算法、数据及原结果校验不变。
3. Web 内嵌同一 Frontend，保留部署/管理/会话与直接 Sidecar 遥测，删除旧预览实现和 9901 监听。通过 `192.162.2.64` 的 Web 部署受管节点并验证同一套结果；多机默认 strict-RDMA，不静默降级。

单机通过只是第一阶段，服务器同页验收通过才算整体完成。不另建发布/校验框架或第三个部署配置模块。F13、F19/F20、G02 的删并去向见 todo，未把取消任务标成完成。

修改范围仍限 web/frontend、案例 Compose 合并及必要部署文档；Sidecar、SDK、协议、案例算法/测试/数据和公共发布脚本不改。不新增账号或录制；不自动提交、发布、停止已有工作负载。详见 plan 第 1 节。
