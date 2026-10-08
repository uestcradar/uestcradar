# 独立 Frontend：实现与 ARM64 验证

执行日期：2026-10-07。完成 F00–F12 的应用实现/验证；F14 正式发布及之后的案例、Web 集成未完成。

## 基线与范围

- 仓库基线：`6644399`，分支 `feat/signalsink`。
- 执行前工作区仅有能力图、规格、tasks 三份文档的未提交调整；已保存，不覆盖这些调整。
- 起点记录：本机 `/tmp/frontend-split.RJ9R7H/` 中的 `base-ref.txt`、`status-before.txt`、`diff-before.patch`、`algorithm.sha256`、`containers-before.txt`。
- 本次新增实现仅在 `workspace/infra/frontend/`；未改 Web 实现、案例 Compose/算法/测试、Sidecar、SDK、Ring、协议或公共发布脚本。
- GFKD 二进制与四份 CSV 的 sha256sum 复核全部通过；原有本机 11 个容器 ID 均仍在运行列表。本次未停止或重启这些容器。

## 环境与镜像

- 本机 x86_64 再次实际运行 ARM64 基座，`uname -m` 为 aarch64；原授权 binfmt 仍有效。
- 构建机 `root@192.162.2.64`：宿主和 Docker 都为 aarch64，Docker 19.03.15，约 30 GiB 可用空间，开始时没有运行中的 Docker 容器。
- 该 Docker CLI 的 `--platform` 返回 `"--platform" is only supported on a Docker daemon with experimental features enabled`。未开启实验功能或修改宿主；核实原生 ARM64 与基座架构后直接运行。Dockerfile 的 FROM 固定 ARM64，RUN 再检查 uname，未使用 AMD64 应用镜像。
- 构建机没有 `docker compose` 插件，已有 `/usr/local/bin/docker-compose`；本阶段只用原生 Docker 构建和冒烟，没有将它当作硬件问题，也没有修改宿主工具。
- 已从 Harbor 实际拉取所需 Sidecar/Source/Worker/Sink/build-base，并检查架构、digest 与入口。见 [镜像记录](image-inventory.txt)。这些是既有镜像核验，不是新 Frontend 发布证据。
- 基座固定：`registry.chengyistudio.com/cxx/web@sha256:52c3755b78f07a64e28b95efccf3d4c70ac97808b65b51bc1e8f8a7829c2835b`。

## 实现

- 独立 Go HTTP/WS 进程、节点配置及健康检查；无 SSH/编排模块或 Docker Socket。
- 复用预览与遥测实现，新增绑定节点/实例/声明 stream 的过滤；处理连接替换、畸形消息与取消，关闭浏览器后释放遥测订阅。
- 复用原波形/RD 渲染；页面从 SidecarHello 获取类型，不依赖 Web 的镜像编排接口。
- 根路径/代理前缀的 HTTP、静态资源及 WS 检查通过；64 位帧 ID/丢弃计数使用字符串，断开清空，过期帧明确标为非实时。
- 原 Web 仍未切换；同一 Frontend 的 Web 内嵌与旧预览移除留在 G01，不能宣称整体分离完成。

## 实际验证

在 ARM 构建机专用 `/root/frontend-split-RJ9R7H/` 工作目录执行；没有改已有远程仓库或业务配置。

```bash
docker build --target test -f workspace/infra/frontend/Dockerfile -t uestcradar/frontend:split-tests .
docker build -f workspace/infra/frontend/Dockerfile -t uestcradar/frontend:split-check .
docker run --rm --network none uestcradar/frontend:split-tests sh -ec \
  'go test -count=30 ./internal/preview ./internal/server; CGO_ENABLED=1 go test -race ./internal/preview ./internal/server'
```

结果：

- TypeScript/Vite 构建通过。
- Vitest：**3 文件、11 测试通过**，含原绘图/协议与新 URL/过期检查。
- Go 测试与 go vet 通过；预览、server 两包重复 **30 次通过**；Go race 检查通过。
- 进程冒烟：健康探测退出 0，/api/node 正确显示绑定节点、connected=false、空 streams；静态页面可取，不把无数据就绪伪称真实预览。
- 原生 ARM64 运行镜像以 `65532:65532` 执行 `/frontend`，SIGTERM 正常退出 0；临时冒烟容器已删除。
- 最终候选 image ID：`sha256:62a538f1c6958dc34f50b552e3d8d884e6a5fe507c9c415490ae63795e0adfad`。这是构建机本地候选，**不是 Harbor manifest digest**。

日志：[构建/测试](frontend-validation.txt)、[运行冒烟](frontend-runtime-smoke.txt)。源码文件哈希：[frontend-source.sha256](frontend-source.sha256)，清单 SHA256 为 `694f7ec4b6fbeb76334b8df98eb46f0c785a674a5d4cf835d581e9f0b4c2eea7`。

## 当前门槛与未做事项

尚未发现明确硬件阻塞。F14 要求已确认的干净源码版本和发布许可；当前实现与计划均未提交，任务仍保留“不自动提交/发布”的约束，因此没有擅自创建提交、推送 Git 或发布 Harbor。

需要确认提交当前实现并发布对应版本后，才能继续本机拉取同一 digest、交付两个单一 Compose 并执行真实案例。未用 save/load、本地候选 tag 或远程冒烟替代本机验收。

**尚未通过**：KT2/KT3 实际浏览器绘图与 60 秒有效帧、真实主链停止预览后的计数增长、两个案例 Harbor 部署复现、Web 内嵌及服务器链路验收。不能将本文件的单元/进程验证抵扣这些项目。
