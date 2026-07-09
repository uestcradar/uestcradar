# Phase 4: 物理分发与容器热重载

只有当 Phase 3 单测全部绿色通过后，才允许执行物理覆盖：

1. **拷贝覆盖**：
   通过物理通道将 `.so` 复制到目标机 `CODE_DIR/lib/du/flowgraph/plugins/` 挂载路径下。
2. **重启热加载**：
   若 `USE_DOCKER=true`，触发宿主机指令：
   ```bash
   docker restart ${DOCKER_NAME}
   ```
   重启后，流图引擎自动载入最新覆盖的算子，完成热重载。
