# Web HTTPS 集成：G03–G10 验证

> 历史严格 TLS 模式记录。用户随后改选可信内网模式：关闭节点证书校验、自动生成自签证书，不再要求 CA/访问规则。当前实现和新增检查以 [web-lan-mode.md](web-lan-mode.md) 为准；下述旧门槛已取消。

**代码与集成测试已完成；G11 发布、G12 真实服务器链路验收未完成。** 本记录不能代替 KT2/KT3 的多机算法、strict-RDMA 或隔离验收。源码未提交、Web 新版未推送 Harbor。

## 实现

- 会话 Get 超时、Delete、定时 expire 统一取消预览连接并清零凭据，续期不误取消。
- 已检查节点的固定 HTTPS 8081 代理，校验 CA/有效期/IP SAN；无 SSH 隧道、环境 HTTP proxy、任意目标或 HTTP 回退。
- Cookie、认证/代理认证及 CSRF 头不交给 Frontend；拒绝跨 Origin、未授权节点及路径逃逸；后端不能设置 Web Cookie。
- 节点 snapshot/ws 复用 Web Store/Hub。绑定来自已部署 Sidecar 的 NODE_ID，部署成功/停止时更新；空绑定失败关闭，不退回全局快照。订阅切换绑定后仅输出新节点；全局流不变。
- 生成 Compose 含只读 TLS 文件、Frontend 固定 digest、Worker 的 Sidecar 健康依赖与原 IPC。Sidecar/Worker 也使用受信 Harbor RepoDigest；缺失须同步/再检查。
- strict-RDMA 仍默认。显式 TCP 才生成 functional/tcp,self，不挂 RDMA 设备、不自动降级。
- 节点详情使用同源 iframe。旧 PreviewPanel/解码/绘图/生成类型、Web preview 接收/转发及 9901 监听已删除。Frontend 原镜像未改。

## 最终原生 ARM 检查

构建机 `root@192.162.2.64`，隔离源码目录 `/root/web-https-g01/repo`。未修改既有仓库或实际部署。

- build-base：`registry.chengyistudio.com/cxx/web@sha256:52c3755b78f07a64e28b95efccf3d4c70ac97808b65b51bc1e8f8a7829c2835b`。
- TypeScript/Vite 构建通过；4 个 Vitest 文件、**22 个测试通过**。
- 全量 Go：**73 个测试/子测试通过**；vet、orchestration/server race 通过。
- 普通无网络测试中，`TestPinnedFrontendImageHTTPS` 明确跳过；它随后/独立在真实镜像与测试 CA 条件下执行，不把跳过算作通过。
- TLS 拒绝、HTTP/WSS、身份/路径/凭据隔离、会话取消、关闭浏览器、慢写超时与管理不阻塞通过。
- 节点遥测的双节点隔离、动态绑定、空绑定、全局订阅保持及取消释放通过。
- TLS 文件权限检查、缺失 CA、默认 strict-RDMA、显式 TCP、Harbor digest 拒绝/生成及 iframe/API 请求契约通过。

```bash
# 原生 ARM 构建机，仓库根目录
docker build --target builder \
  --build-arg GO_BASE=registry.chengyistudio.com/cxx/web@sha256:52c3755b78f07a64e28b95efccf3d4c70ac97808b65b51bc1e8f8a7829c2835b \
  -f workspace/infra/web/Dockerfile -t uestcradar/web:https-g01-tests .
docker run --rm --network none --entrypoint sh uestcradar/web:https-g01-tests \
  -ec 'test "$(uname -m)" = aarch64; go test -timeout 90s -json -count=1 ./...; go vet ./...; CGO_ENABLED=1 go test -timeout 90s -race ./internal/orchestration ./internal/server; cd frontend; npm test'
```

## 原 Frontend 镜像与浏览器

使用已发布的同一 Frontend：

`registry.chengyistudio.com/cxx/frontend@sha256:7784fe19bc47d1509705d347595d9e92254b59e2efb2c1b446bc63281ee28052`

1. ARM 镜像监听 localhost HTTPS 8081；只读挂载独立测试 CA/服务器证书/密钥，非 root 65532:65532。
2. **镜像内 /frontend --healthcheck 通过**，证明现有 SSL_CERT_FILE 足够，无需改 Frontend 或发布新镜像。
3. `TestPinnedFrontendImageHTTPS` 验证同源前缀的真实页面、相对 JS/CSS、api/node、healthz、WSS 升级和会话取消。
4. Chrome 使用专用 HOME/profile，通过已有 NSS 库导入单个测试 CA；没有修改系统信任，没有忽略证书错误、SPKI 例外或 SSH 预览隧道。
5. 浏览器访问临时 TLS 测试入口 `https://192.162.2.64:18443/`。实际加载 Web 页面与 Frontend iframe，显示 `smoke-node`、真实 Sidecar Hello/实例、Type 1:3 输入 Leg 和“预览通道已连接”；捕获同源 WSS，JS 异常为零。
6. Sidecar 使用既有 ARM64 Harbor digest `sha256:d400a3b523fd1868b21d4a479552ef9de6bfec4ead6940dabae611a052f4f20c`，仅测试私有 SHM/localhost TCP，没有 Source/Worker 或算法帧。

**浏览器夹具的会话、主机探查和空遥测为测试替身。** 首次夹具漏设 RemoteBackend，被页面自动探查触发空指针；修复夹具后复跑通过。没有因此放宽生产认证。该截图只证明 TLS、嵌入、真实 Hello/预览订阅，不证明图像计算或 G12。

浏览器构建对应源码哈希单独保存。其后补充 Harbor manifest pins、快照写入期限以及“下次部署”文字/API 测试，已完成最终全量 ARM 回归；未把历史截图标作最终源码的数值验收。

## Web 运行镜像 smoke

原生 ARM scratch 镜像已构建：本地 **Image ID** `sha256:10cc29c22cba9aa41b12c3582d24d3f63f69536ee18b6242290f90783aee8711`，不是 Harbor manifest，不用于部署证据。

非 root `65532:65532`、入口 `/telemetry`；localhost API 可用，旧 `/ws/frames` 为 404，未登录节点代理为 401，9901 未监听，SIGTERM 退出 0。

## 清理与尚缺前提

临时 Web/Frontend/Sidecar 容器、临时监听、Chrome 及测试签发/服务器私钥已清理；未改宿主 TLS、信任库或防火墙。既有算法、SDK、Sidecar、协议及公共发布脚本未改。

[只读前置检查](web-deployment-prerequisites.txt)：`.64` 的 hns_1 ACTIVE/LINK_UP，尚无明确硬件阻塞。但计划路径缺 Frontend TLS 三文件和 Web 的节点 CA bundle；既有 Web 私钥为 0644；INPUT 默认 ACCEPT，8081 的仅 Web 可访规则未验证，外部 ACL 也未验证。目标服务器清单仍未指定。不能自动选机、改宿主权限/网络、以测试 CA 替代生产信任或宣称交付完成。

## 原始证据

- [最终构建](web-frontend-build.txt)、[最终测试](web-frontend-tests.txt)、[当前 Web 源码清单](web-frontend-source.sha256)
- [真实 Frontend TLS/API/WSS](web-frontend-image-smoke.txt)
- [浏览器构建与源码](web-browser-source.txt)、[浏览器运行](web-browser-validation.txt)、[DOM/WSS 记录](web-browser-result.json)、[截图](web-browser.png)
- [Web 运行 smoke](web-runtime-smoke.txt)
