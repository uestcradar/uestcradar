# Web build-base 发布

`workspace/infra/web/docker/Dockerfile.build-base` 是唯一 Web 编译基座入口。基座包含：

- Go 1.24、Protobuf compiler、`protoc-gen-go`
- Node.js、npm
- 与 `frontend/package-lock.json` 对应的离线 `node_modules`

既有发布流程保留以下标准 Tag。本次 HTTPS 集成代码尚未发布；验收必须另记录获批的新不可变版本/digest，不把现有 `latest` 当作本次实现，也不擅自覆盖现有版本：

```text
registry.chengyistudio.com/cxx/web:build-base
registry.chengyistudio.com/cxx/web:latest
```

## 在 x86 构建机更新 ARM64 build-base

仅在 Go/Node/前端依赖发生变化时执行：

```bash
docker buildx build \
  --platform linux/arm64 \
  --progress=plain \
  -t registry.chengyistudio.com/cxx/web:build-base \
  --push \
  -f workspace/infra/web/docker/Dockerfile.build-base .
```

## 在 ARM64 发布业务镜像

业务 Dockerfile 从 build-base 复用 Go modules 和前端依赖，不在 ARM 发布机执行
`apt install` 或在线 `npm install`。若 `package-lock.json` 与基座不一致，构建立即失败并要求
先更新 build-base。

```bash
./.agents/skills/docker-release/scripts/release.sh \
  --remote-dir /root/workspace/uestcradar
```

交互菜单选择 `Web`。发布脚本会检查 Node、npm 和离线前端依赖目录。

## 拉取与运行最新 Web 运行镜像

使用以下命令从私有镜像仓库拉取最新的 Web 控制面镜像并启动容器（请将 `TELEMETRY_ADVERTISE_HOST` 替换为 Web 服务器在局域网中的真实物理 IP）：

```bash
# 1. 登录私有镜像仓库（如尚未登录）
docker login registry.chengyistudio.com

# 2. 强制拉取最新 Web 运行镜像
docker pull registry.chengyistudio.com/cxx/web:latest

# 3. 后台启动 Web 容器（此配置启用 8080/HTTPS；9900/UDP 仍独立接收遥测）
docker run -d \
  --name uestcradar-web \
  --network host \
  -e TELEMETRY_ADVERTISE_HOST=192.162.2.64 \
  -e TELEMETRY_TLS_CERT_FILE=/etc/uestcradar/tls/tls.crt \
  -e TELEMETRY_TLS_KEY_FILE=/etc/uestcradar/tls/tls.key \
  -v /etc/uestcradar/tls/tls.crt:/etc/uestcradar/tls/tls.crt:ro \
  -v /etc/uestcradar/tls/tls.key:/etc/uestcradar/tls/tls.key:ro \
  registry.chengyistudio.com/cxx/web:latest

docker start uestcradar-web
```

## 内网 HTTPS 节点预览（新版本，自动准备）

- 浏览器只访问 Web；详情 iframe 使用 `/api/v1/nodes/{ip}/frontend/`，Web 通过 HTTPS/WSS 直连该节点管理 IP 的 8081。SSH 仅用于检查和部署，无预览隧道。
- 按用户确认的可信内网模式，Web→Frontend 使用 HTTPS/WSS，但不校验证书链、有效期或 SAN。没有 CA 配置、签发服务或白名单配置页面。仅提供加密、不验证节点身份，不面向公网。
- 部署自动生成自签证书，经 SSH/SFTP 上传到 `/root/workspace/docker/frontend-tls/<revision>/{server.crt,server.key}`；只读挂载，私钥为 UID/GID 65532:65532、0400。无 CA 签发私钥；不打印证书私钥、不进入镜像。
- Frontend 的 SSL_CERT_FILE 指向自身 server.crt，仅用于原镜像的本地 healthcheck。Web 对浏览器的 TLS、已有 SSH 登录和主机指纹确认不变。
- 不修改宿主防火墙，不要求用户配置 8081 来源限制；仅在可信内网部署。Sidecar 预览只到本机 9903，全局遥测仍直达 Web 9900。
- Sidecar/Worker 先通过现有同步/检查流程取得 Harbor RepoDigest；生成配置使用 manifest digest，不回退到本地镜像 ID、dev 标签或临时构建。Frontend 使用已验收 digest。
- 默认 strict-RDMA；选 TCP 必须显式操作，生成 functional/tcp,self 且不挂 RDMA 设备。刷新后选择器恢复 strict-RDMA 默认值；它表示下次部署配置，当前实际运输方式以链路遥测为准。
- 每次部署自动生成新的十年期自签证书；revision 卷路径变化让 Compose 更新 Frontend，不为证书单独重启主链。保留旧版本目录，避免影响仍在运行的容器；不需手工续期或重启 Web 加载信任。

节点从现有列表探查选择；仍需要可用的已有 SSH 凭据，不覆盖未知业务。证书、CA、8081 白名单不再是执行门槛。新版本移除 Web 9901 和旧预览实现，无双路径兼容。

## PCIe Source 自动设备配置

选中已批准的 PCIe Source 后，Web 自动给该节点的 **worker-node** 添加 `/dev/mem:/dev/mem:rw` 和 `SYS_RAWIO`；用户无需手工改 Docker。部署前只读核对 `/dev/mem`、`04:00.0` 板卡 `10ee:7038` 及 BAR0 `0xef000000`，不自动修复权限、安装驱动或改变硬件配置。平台 DMA 预留内存等前置条件仍须符合该已验证镜像的部署要求。

硬件授权绑定 `internal/orchestration/planner.go` 中 `pcieSourceReference` 的不可变 digest，并核对 Entrypoint、无覆盖 Command、ARM64 和 source/none→4:1 契约。仅改标签或伪造 Entrypoint 不会获得权限；新版 PCIe Worker 必须审核并更新此引用。PCIe Worker 设为不自动重启，异常后保留诊断，不重复初始化板卡或假装录制连续。

SignalSink、普通 Worker、Frontend 不获得该权限；Sidecar 保留原有 RDMA 设备配置。SignalSink 的捕获目录与录制控件保持原实现。Web 不设置线程 CPU 绑定；已批准的新版 PCIe Source 自行校验并绑定采集/DMA/输出角色（当前平台默认 8/9/10）。不改变系统全局调度配置。

推荐拓扑为 `.64` PCIe Source → strict-RDMA → `.80` SignalSink，每个节点各自运行 Worker/Sidecar/Frontend。Web 更新会清空内存 SSH 会话，需重新登录及确认节点指纹；不需要再次手工配置容器设备。

## 更新节点 Frontend

已探查节点的操作区包含“更新 Sidecar / 更新 Worker / 更新 Frontend”。Frontend 更新复用现有登录、SSH 指纹确认、异步任务和控制台日志；拉取 Web 当前配置的已发布 Frontend digest，并检查 ARM64、入口及运行用户。不是选择任意 latest 标签，也不新增凭据。

与现有镜像同步按钮一致，该操作仅同步节点镜像，不重启运行容器，不修改证书/Compose，不打断算法流。需要切换运行版本时，通过正常部署流程应用。

## 刷新后保留拓扑

拓扑自动保存在当前浏览器、当前访问地址的 localStorage 中，包含节点顺序、Worker 镜像、RDMA 端口及 Ring 参数。刷新会恢复配置和仍有效的登录会话；会话过期时需要重新登录，拓扑仍保留，登录后会重新登记缺失节点并探查。离线节点的已选值会保留并显示“待探查确认”。刷新不会自动部署。刷新后会重新探查拓扑节点、接收实时遥测，并恢复此前打开的详情抽屉和输入输出预览订阅；预览图等待新的帧到达后显示。

密码、私钥、任务和运行状态不写入 localStorage。更换浏览器或访问地址、清除站点数据后，不会保留该拓扑；此前已丢失的拓扑需要重新配置一次。此功能无需增加容器数据挂载。

保存 SSH 凭据仅创建会话，探查节点时才验证连接。认证失败会提示失败节点并重新打开凭据输入框；也可点击顶部“重新登录”替换会话。更新凭据后只重新探查，部署、停止和镜像更新需手动重新执行。指纹未确认时单独提示确认；重建 Web 会清空内存会话，需要重新输入凭据并确认指纹。
