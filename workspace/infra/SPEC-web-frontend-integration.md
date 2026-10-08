# Spec: Web 内嵌独立 Frontend

模块：`web-frontend-integration`，见 [能力图](CAPABILITY_MAP.md)。实施已获确认；代码与 ARM 检查已完成，正式发布/服务器算法验收尚未完成。

## 1. 当前操作原则

用户最新确认：这是可信内网的算法流工具，以方便使用为先。自动从 Web 已有可用节点列表探查、选择节点；不再要求用户提供服务器清单或配置证书、CA、8081 访问规则。

**Web→Frontend 保留 HTTPS/WSS 加密，但不校验证书链、有效期或主机名。** 这是明确选择的内网模式，不提供节点身份保证，不面向公网。此前“必须预配 CA、验证节点证书、仅 Web 可访”的部署门槛已取消；历史测试记录保留，但不作为当前策略。

仍保留现有 SSH 凭据、主机指纹、Web 会话/Origin/CSRF、明确节点目标和覆盖已有部署确认。关闭节点 TLS 校验不等于关闭 SSH 登录。不可用凭据不得靠猜测或反复尝试绕过。

## 2. 目标与范围

- Web 运行于 `192.162.2.64`，保留拓扑、检查、SSH/Compose 部署、会话和全局遥测。
- 节点详情 iframe 使用单机验证过的同一个 Frontend 镜像、页面、解码和绘图。
- 节点卡片提供并列的“更新 Sidecar / 更新 Worker / 更新 Frontend”。Frontend 同步复用会话、已检查节点授权、异步任务/日志及错误反馈；拉取 Web 当前配置的已发布 digest，验证 ARM64、入口和运行用户。与现有镜像更新语义一致，仅同步镜像，不自动重启或重部署；后续部署使用该版本。
- 删除 Web 旧预览接收/转发、重复 UI/生成类型与 9901 监听，无双实现开关。
- Frontend 故障、慢浏览器或关闭页面不影响 Worker/Sidecar 主链及 Web 全局管理。
- 不修改算法、SDK、Sidecar、Ring、协议、案例数据/测试或公共发布脚本；不新增框架、CA 服务、认证系统或部署工具链。

## 3. 路由与遥测

入口 `/api/v1/nodes/{ip}/frontend/`，尾部 `/` 固定，相对资源 URL 保持有效。

- 节点必须属于会话允许且已检查的节点，拒绝任意 URL、端口和路径逃逸。
- 页面、assets、api/node、healthz、ws/frames 由标准 ReverseProxy 经 HTTPS 直达固定管理 IP:8081。关闭证书校验仅作用于此 Transport，不创建 SSH 隧道，不回退明文 HTTP。
- 不向 Frontend 传递 Web Cookie、认证/代理认证、CSRF 头或 SSH 凭据，不允许其覆盖 Web Cookie。
- 会话到期/退出取消连接；连接、响应和写入有界，不持有全局管理锁等待网络。
- 前缀 api/snapshot 与 ws 由 Web Store/Hub 提供，按已部署 NODE_ID 过滤；空绑定不退回全局流。检查、部署和停止更新节点绑定。
- Sidecar 全局遥测继续直达 Web 9900/UDP；Frontend 不作为全局中转。单机 Frontend 原有遥测接口不变。

## 4. 自动证书与节点部署

- Web 用 Go 标准库在部署时生成每节点自签 Ed25519 证书/密钥，不依赖节点 openssl，不要求 CA。
- 经既有 SSH/SFTP 上传到 `/root/workspace/docker/frontend-tls/<revision>/`。私钥 UID/GID 65532、0400，只读挂载到对应 Frontend；不打印、不进入镜像或浏览器。无 CA 签发私钥。
- revision 来自证书哈希；文件写完才进入 node.env 和 Compose，版本目录避免替换运行中容器引用的旧证书。卷路径变化让 Compose 更新 Frontend，不为证书单独重启主链。
- Frontend 使用既有 TLS 配置，SSL_CERT_FILE 指向自身证书供本地 healthcheck 使用。这是内部实现，不增加用户配置项或证书页面。
- 不自动修改宿主防火墙；只在可信内网使用，用户不需要设置 8081 白名单。
- Frontend HTTPS 监听选定管理 IP:8081，UDP 127.0.0.1:9902，preview TCP 127.0.0.1:9903；Sidecar 预览发本机 9903。
- Frontend 不共享 Worker IPC、不挂 SSH key/Docker Socket/工作目录；主链不依赖 Frontend 健康状态。Worker 保持 `ipc: service:sidecar-node` 和 Sidecar 健康依赖。
- 应用使用 ARM64 Harbor manifest digest；缺少 RepoDigest 要同步/再检查，不回退 dev、本地临时镜像或节点构建。
- 跨机默认 strict-RDMA。显式 TCP 才生成 functional/tcp,self，去除 RDMA 设备挂载，不自动降级。

## 5. 执行与验收

实现及任务见 [plan](tasks/plan.md)、[todo](tasks/todo.md)，测试见 [当前内网模式](tasks/evidence/web-lan-mode.md) 和 [先前集成验证](tasks/evidence/web-frontend-tests.md)。

1. 原生 ARM 构建、Go/Vitest/vet/race；测试未知 CA、过期/SAN 不匹配证书在内网模式可用，但连接仍为 TLS。
2. 自动证书可被现有 Frontend 加载，本地 healthcheck 能信任自身证书；SFTP 安装和文件权限可运行验证。
3. 会话/节点/Origin/路径隔离与取消保留；同源 assets/API/WSS、真实浏览器 iframe 正常；全局遥测不受节点订阅影响。
4. Web 运行保留 8080/9900，无 9901；所有旧预览调用点移除。
5. 从已有列表选择可登录、满足架构/网络要求且不会覆盖业务的节点。现有模型每 IP 一个节点，按链路所需数量选择；SSH 失败或网络不可达如实报告，不冒充硬件故障。
6. 通过正式 Web 管理的服务器链路运行 KT2/KT3，真实 `1:3→2:2` 与 `2:2→3:2`，至少 60 秒、输入输出各两个有效帧，原算法/Sink 校验通过。
7. 关闭浏览器、慢消费者、Frontend 停 30 秒，主链/全局遥测继续；30 秒内恢复有效预览，主链容器不因预览测试重启。strict-RDMA 要有实际运输证据。

## 6. 当前边界

无需再审批上述编码和自动选机/证书机制。保持源码范围与已有工作负载；覆盖现有业务、修改宿主驱动/Docker 等仍需确认。正式提交/发布走已有授权和不可变版本要求，不以本地测试镜像冒充 Harbor 交付。

目前九个列表节点中，已有 SSH 访问方式仅能登录 `.64`；其余节点返回认证失败或 No route to host。证书/CA/防火墙不再是前提，需要的是可用的既有 SSH 凭据/连接条件。G01 尚未整体完成。
