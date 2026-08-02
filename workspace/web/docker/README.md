# Web build-base 发布

`workspace/web/docker/Dockerfile.build-base` 是唯一 Web 编译基座入口。基座包含：

- Go 1.24、Protobuf compiler、`protoc-gen-go`
- Node.js、npm
- 与 `frontend/package-lock.json` 对应的离线 `node_modules`

运行镜像仍只使用两个标准 Tag：

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
  -f workspace/web/docker/Dockerfile.build-base .
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

# 3. 后台启动 Web 容器（共享宿主机物理网络，直通 8080/HTTP 与 9900/UDP）
docker run -d \
  --name uestcradar-web \
  --network host \
  -e TELEMETRY_ADVERTISE_HOST=192.162.2.64 \
  -e TELEMETRY_TLS_CERT_FILE=/etc/uestcradar/tls/tls.crt \
  -e TELEMETRY_TLS_KEY_FILE=/etc/uestcradar/tls/tls.key \
  -v /etc/uestcradar/tls:/etc/uestcradar/tls:ro \
  registry.chengyistudio.com/cxx/web:latest

docker start uestcradar-web
```
