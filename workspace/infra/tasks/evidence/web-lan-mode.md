# Web 内网简化模式：自动证书、节点访问检查

用户已确认便捷优先：保留 HTTPS，但关闭 Web→Frontend 证书校验；证书自动准备，不要求 CA、证书页面、8081 白名单或用户指定节点清单。

## 已完成

- 仅节点代理 Transport 设置 InsecureSkipVerify；固定 HTTPS 8081、会话/节点/Origin/路径限制、超时与凭据剥离保持。
- 部署使用 Go 标准库生成独立自签 Ed25519 证书和密钥，无外部命令/CA。十年有效期，部署时重新生成。
- 既有 SFTP 上传版本目录；证书哈希 revision 写入 node.env，仅在文件全部就绪后更新部署配置。卷路径变更让 Compose 更新 Frontend，避免替换正在使用的证书。
- UID/GID 65532:65532、私钥 0400，只读挂载。Frontend healthcheck 用自身证书作为 SSL_CERT_FILE，无 CA 签发私钥。未改 Frontend 镜像、算法或 Sidecar。
- 原生 ARM 完整构建通过；**74 个 Go 测试/子测试、vet、race、22 个 UI 测试通过**。新增生成证书、独立密钥、健康检查信任、真实 SFTP 写入/权限检查；未知 CA、过期和错误 SAN 在内网模式成功，连接仍为 TLS。
- 原 Harbor Frontend 镜像实际运行：加载本实现自动生成的自签证书，镜像内 `/frontend --healthcheck` 成功；Web 没有 SSL_CERT_FILE/CA 配置时，真实页面、assets、api/node、healthz、WSS 和会话取消通过。
- 测试容器与自动生成的测试私钥已清理。无宿主防火墙、既有 Web TLS 或业务部署改动。

## 节点探查

从代码 DefaultNodeIPs 读取现有九节点名单，使用现有 root SSH 身份和严格主机密钥检查，不猜密码、不修改远程认证。

| 节点 | 当前访问结果 |
|---|---|
| 192.162.2.64 | 可登录，aarch64，Docker 19.03.15，无运行容器，hns_1 ACTIVE/LINK_UP |
| 192.162.2.16、.32、.80、.128、.160 | Permission denied (publickey,gssapi-keyex,gssapi-with-mic,password). |
| 192.162.2.144、.176、.192 | No route to host |

也检查了管理机 .64 到部分节点的现有 SSH：.16 认证失败，.32/.80 无已知主机密钥，未自动接受或修改信任。

目前不能把其余节点判定为空闲或可部署，也不能在未登录时替用户选择/覆盖工作负载。需要可用的**既有 SSH 访问方式**；不是新的证书/CA/防火墙配置，也没有证实硬件故障。真实多机算法流与正式发布仍未完成，未把单个 Frontend smoke 冒充 G12。

## 证据

- [构建](web-lan-build.txt)、[完整测试](web-lan-tests.txt)
- [原 Frontend 镜像自动证书/HTTPS/WSS](web-lan-image-smoke.txt)
- [九节点探查](web-lan-nodes.json)
- [当前源码哈希](web-lan-source.sha256)

先前严格 TLS 的浏览器/测试记录继续保留作历史；本次尚未重跑真实算法浏览器链路。关闭校验只意味着加密、不证明节点身份，限可信内网。
