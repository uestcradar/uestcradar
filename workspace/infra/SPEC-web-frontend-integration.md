# Spec: Web 内嵌独立 Frontend

- 模块：`web-frontend-integration`，见 [能力图](CAPABILITY_MAP.md)。
- 状态：**待用户审定；未实施、未部署。** 单机前置证据见 [验收](tasks/evidence/acceptance.md)。
- 依赖：已验证的 Frontend `@sha256:7784fe19bc47d1509705d347595d9e92254b59e2efb2c1b446bc63281ee28052`。

## 1. 目标

Web 保留拓扑、主机检查、SSH/Compose 部署、会话与全局遥测。节点详情嵌入单机使用的同一 Frontend 镜像和页面，删除 Web 内重复预览解码、绘图、转发和 9901/TCP 监听。

成功必须来自 Web 实际管理的服务器链路，不是跳转链接、mock、健康检查或本机截图。Web 运行于 `192.162.2.64`，Worker 可位于已选服务器。该阶段不增加算法、录制、SDK 输出或 RDMA 性能压测。

## 2. 提请确认的假设

1. 每个被选服务器继续按现有模型部署一个节点，复用既有会话中的 SSH 凭据与已确认 host key；不增加凭据来源或认证系统。
2. 节点 Frontend 保持 loopback 绑定，Web 通过已有 SSH 连接能力访问，而非开放节点 HTTP 端口或分发新的 TLS 私钥。若不采用此方案，需先确认节点 TLS 与访问控制方案，不能直接关闭 TLS 校验。
3. 在 Web 内选择具体服务器后才部署，不自动挑选其他主机或替换已有工作负载。现有 planner 拒绝同 IP 重复，KT2/KT3 分别需要三个/四个选定节点。
4. 跨机默认 strict-RDMA；TCP 只能显式选择，不能因设备或网络失败自动降级。

## 3. 界面与接口边界

### 同源页面

- 在现有节点详情中用 iframe 承载 Frontend，建议入口 `/api/v1/nodes/{ip}/frontend/`，规范为尾部 `/`。
- 入口位于既有会话保护下。IP 必须属于当前会话允许且已检查的节点；不接受客户端提供的任意目标 URL、Host 或端口。
- 页面、assets、`/api/node`、`/healthz`、`/ws/frames` 经同一前缀代理。标准库 `httputil.ReverseProxy` 负责 HTTP/WebSocket，剥离前缀后通过 SSH TCP channel 访问节点 `127.0.0.1:8081`。
- 保留 Origin 校验；不把 Web 会话 Cookie、认证头或 SSH 凭据转发给 Frontend。会话到期/退出后拒绝新请求并终止其代理连接。
- SSH dial、请求、写入与断开有界且可取消；预览连接不占用全局管理锁。关闭节点详情释放连接；节点故障显示不可用，不阻塞主机检查/其他节点。

### 遥测不经过 Frontend

- Sidecar 全局遥测仍直接发往 Web `9900/UDP`，不复制 UDP 转发器。
- 嵌入前缀下的 `/api/snapshot` 与 `/ws` 由 Web 已有 Store/Hub 提供，并限定该节点；沿用原 JSON 结构，无新数据协议。
- 独立 Frontend 自身的接口不变，同一页面仍使用相对 URL。单机模式继续由 Frontend 接收该节点 UDP。
- Web 整体状态、拓扑和主机检查不依赖 Frontend 的存在。

### 节点部署

- 现有生成式 Compose 增加一个 Frontend 服务，固定已验证 ARM64 digest。
- Sidecar 预览改为 `127.0.0.1:9903`；Frontend HTTP/WS `127.0.0.1:8081`、UDP `127.0.0.1:9902`、preview TCP `127.0.0.1:9903`。服务器模式不把全局遥测改发到 9902。
- Frontend 不共享 Worker IPC，不挂 Docker Socket、SSH key 或工作目录；Worker/Sidecar 不依赖其健康状态。
- 延用现有远程检查、Compose CLI 识别、上传/验证/启动流程；只增加必要镜像核验和服务，不建立第二套部署工具。镜像不支持 Frontend 新契约也不伪装成 web/v1。
- 发布和部署仍走 ARM 构建测试→Harbor→拉取 digest，不在节点构建，不覆盖已有不可变版本。

### 切换后删除

删除 Web 的 preview TCP 服务、旧 `/ws/frames` 全局转发、旧 PreviewPanel/preview/rdHeatmap 及仅供它们使用的生成类型和测试。保留有用途的 telemetry protobuf、Store/Hub、编排、会话和 SSH；不留运行时双实现或旧端口兼容路径。

## 4. 技术栈与代码落点

不引入依赖：既有 Go 1.24、net/http/httputil、x/crypto/ssh、Gorilla WebSocket、React 18/TypeScript/Vite/Vitest。

- `web/internal/orchestration/{remote,session,http,planner,types}.go`：会话授权、固定目标代理、Frontend 部署信息与原 SSH 通道。
- `web/internal/server/{server,hub,store}.go`：移除旧入口、复用节点级遥测。
- `web/frontend/src/App.tsx`：详情 iframe；删除旧预览专属实现。
- `frontend/`：原则上不改运行实现；仅当已批准接口确需最小调整时修改，并重新走同一 ARM 发布与案例回归。
- 规格、任务与脱敏证据均位于 `infra/`；不修改算法、SDK、Sidecar、协议、案例测试或公共发布脚本。

## 5. 代码风格

沿用 Go/TS 现有组织和格式。授权检查先于连接；失败显式返回，不回退到旧 Web 绘图。例如接口行为：

```go
// 在已有会话 handler 内完成节点授权后才建立代理。
if !allowedNode(session, ip) {
    http.Error(w, "node unavailable", http.StatusForbidden)
    return
}
```

示例仅说明边界，不要求新增一层通用代理框架。优先复用现有 SSH/client、会话锁与检查结果。

## 6. 开发命令与发布前验证

审批后，在原生 ARM 构建机仓库根目录运行既有构建与测试：

```bash
test "$(uname -m)" = aarch64
docker build --target builder \
  --build-arg GO_BASE=registry.chengyistudio.com/cxx/web@sha256:52c3755b78f07a64e28b95efccf3d4c70ac97808b65b51bc1e8f8a7829c2835b \
  -f workspace/infra/web/Dockerfile -t uestcradar/web:integration-test .
docker run --rm --network none --entrypoint sh uestcradar/web:integration-test \
  -ec 'go test -count=1 ./... && go vet ./... && cd frontend && npm test'
```

最终应用按既有 Web 发布流程使用新的不可变版本；发布前记录实际源码版本与测试证据。目标标签及服务器部署清单在任务审批时固定，不在规格中虚构 digest 或运行成功。

## 7. 测试与成功条件

复用既有 Go/Vitest 测试，补边界测试，不新增框架或发布/审计脚本：

1. 无会话、跨 Origin、未授权 IP、任意 URL/端口和路径逃逸请求被拒绝；SSH host key 不匹配仍失败；退出/过期关闭预览连接。
2. 同源前缀下页面、JS/CSS、API 和二进制 WS 工作；独立直达保持原行为。iframe 内真实节点/Leg/类型正确，换节点不残留旧帧。
3. Web 8080 的控制台/API/代理保留，9900 UDP 持续接收全局遥测；9901 不再监听，无旧渲染器继续运行。
4. 通过 Web 在明确选定的服务器部署真实 Source/脉压/GFKD/Sink。显示真实 `1:3→2:2` 与 `2:2→3:2`，至少 60 秒、输入输出各两个不同有效 frame_id，原校验通过。核对值、维度、坐标/池化/色标语义，不要求不同运行时刻截图像素相同。
5. 浏览器关闭、慢消费者及 Frontend 停 30 秒时，主链计数和 Web 全局遥测仍增长；30 秒内恢复有效预览，Worker/Sidecar 不因预览测试重启。
6. 全部应用镜像为 ARM64 Harbor digest。strict-RDMA 有实际运输证据；明确失败就停止，不伪称 TCP 为 RDMA，不新增性能压测。

## 8. 边界

- **Always**：用原认证/信任边界，校验目标、超时与取消；保护既有工作负载；留下真实部署和截图证据；失败如实记录。
- **Ask first**：本规格与目标服务器清单；替换既有部署；更改宿主 Docker、网络、驱动、TLS 或账号；新增依赖或扩大源码范围。
- **Never**：在 Frontend 放 SSH 凭据；绕过 TLS/Origin/会话/CSRF；任意目标代理；用 Frontend 中转全局遥测；偷偷 TCP 降级；修改算法以迁就旧 Sink；把本机成功当作服务器验收。

## 9. 实际前置检查与待确认

`.64` 的原生 ARM64 构建和 SSH 已验证；只读检查发现 hns_1 为 ACTIVE/LinkUp，10 Gb/s，RDMA 地址 `192.170.2.64`；hns_0 为 DOWN。Docker 19.03.15，已有 Compose v1.29.2，现有 Web 后端本来支持 v1/v2 识别。未升级宿主或宣称跨机 RDMA 已通过。

待用户确认：

1. 是否接受上述“Web 同源 + 既有 SSH 通道 + 节点 loopback Frontend”的集成方案？
2. KT2/KT3 分别使用哪些服务器？现有可选列表来自 `DefaultNodeIPs`；没有据此擅自部署任何节点。

审定后再推进本模块 Plan/Tasks 与实现，不能将 G01 勾选为已完成。
