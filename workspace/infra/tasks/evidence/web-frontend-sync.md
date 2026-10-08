# 节点 Frontend 镜像同步

新增与 Sidecar/Worker 同级的“更新 Frontend”按钮及 POST `/api/v1/orchestration/images/frontend/sync`。

复用会话/CSRF、已检查节点限制、SSH Host Key、任务进度和日志。Sidecar/Frontend 共用镜像同步任务流程；Frontend 拉取与部署复用同一个固定发布 digest 和 ARM64/入口/用户检查。操作仅同步镜像，不更改运行中的容器、部署配置或证书。

验证：原生 ARM 构建、Go/vet/race、24 项 UI 测试通过。API 覆盖成功、拉取失败、未检查节点、未授权节点、缺 CSRF；验证节点绑定保留且没有调用启动/停止/配置上传。

已更新 `.64` Web；用真实 SSH 会话确认已知指纹、探查节点，再调用新 API，同步任务 completed，消息为 `Frontend image synchronized (running containers unchanged)`。验证前后 `.32/.64/.80` 的九个 Worker/Sidecar/Frontend 容器 ID 和 StartedAt 一致。

当前页面资源 `/assets/index-MBAf_s3q.js` 包含新按钮及接口。旧 Web 保留为停止的 `uestcradar-web-before-frontend-update-20261007`。新 Web 会话需重新登录；未把这次本地运行更新记作 Harbor 正式发布。

本次原始记录位于 `/tmp/web-frontend-update/`：`tests.txt`、`live-api.json`、`main-before.json`、`main-after.json`、`deploy.txt`。测试会话已删除，未把 SSH 凭据写入证据。
