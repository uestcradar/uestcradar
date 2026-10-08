# Web 自动部署 PCIe Source

状态：用户已批准实施；范围仅 `workspace/infra/web/`。

## 目标与边界

通过现有 Web 登录、SSH 指纹确认、镜像探查和计划部署，在采集节点自动给 PCIe Source 提供硬件访问；用户无需手工改 Docker。保存节点只运行普通 SignalSink，不获得 PCIe 权限。保持 strict-RDMA，不自动回退 TCP。

- 只授予已批准的 Harbor 不可变 PCIe Worker digest，并核对 ARM64、唯一 `/app/pcie_source` Entrypoint、无覆盖 Command、source/none→4:1 契约。不能仅凭镜像名称或 Entrypoint 授权。
- 当前批准 digest：`sha256:5cf489a8efd621ee1a1ce5f54a6e33ee8a16d0eca114e40c49d90a00fc1a330b`。未来 Worker 版本必须审查后更新 Web 的批准引用。
- 仅 worker-node 增加 `/dev/mem:/dev/mem:rw` 和 `SYS_RAWIO`，不使用 privileged、不扩展到 Sidecar/Frontend/SignalSink。PCIe Worker 异常后不自动重启，避免反复初始化硬件及混合录制会话。
- 部署前只读检查 `/dev/mem` 可访问、已知板卡 vendor/device/BAR；不自动 chmod、安装驱动、修改固件、寄存器配置或系统参数。
- 不修改 PCIe Source、SignalSink、SDK、Sidecar；不实施 CPU 绑定，不更改已有录制格式或通用 Worker 参数规则。
- 新 Web 同时交付已实现的 SignalSink 控件；不重写界面。替换管理 Web 前保留旧镜像/容器回退，告知会话需重新登录。

## 计划与任务

1. [x] 增加规划回归：批准镜像得到准确权限；伪造/未知版本不获授权；普通 Source/Sink 不带权限；strict-RDMA/TCP 都不误改其他服务。
2. [x] 实现 Web 内部固定批准规则、Compose 渲染和只读部署前检查；保持会话、CSRF、SSH 授权和类型匹配检查。
3. [x] Go 全量测试/vet/orchestration race 通过；前端 29/29 与构建通过。新增测试先在旧实现失败，再通过；更新部署说明。
4. [x] 正式 ARM/Harbor 发布并更新 `.64` Web；正常 Web 编排 `.64`→strict-RDMA→`.80` 已实际运行，短录制文件校验通过。Source 随后在 18.5668 秒配对失败，60 秒未开始；不宣称采集稳定性修复，详见 [验证报告](tests/pcie-results.md)。

测试：新检出先在 Web 目录执行 `protoc -I ../proto --go_out=. --go_opt=module=uestcradar/telemetry ../proto/telemetry.proto` 并构建前端生成嵌入资源；然后 `go test ./...`、`go vet ./...`、`go test -race ./internal/orchestration`；前端目录 `npm test -- --run`、`npm run build`。沿用现有 Go 测试夹具和固定镜像引用模式，不新增依赖或通用设备授权平台。
