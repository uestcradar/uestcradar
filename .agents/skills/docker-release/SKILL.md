---
name: docker-release
description: 指导开发人员如何编译、打包打标 (包含版本 Tag 与 :latest 覆盖 Tag) 并推送 Sidecar、Cascade Worker 和 Telemetry Web 的生产级 Docker 镜像至私有仓库 (registry.chengyistudio.com)。
---

# Docker 镜像双 Tag 编译与发布指南 (Docker Release)

本 Skill 规范项目三大组件（`sidecar`、`cascade-worker`、`telemetry-web`）的标准 Docker 构建与发布流程。

为了遵循渐进式暴露（Progressive Disclosure）原则，具体各组件的编译指令、Tag 打标与推送细节统一由 `references/` 下的子文档维护。

---

## 1. 核心发布原则

1. **双 Tag 机制**：每次镜像发布时，必须同时维护并推送**审计版本 Tag**（`dual-leg-${GIT_SHA}-arm64`）与**生产最新 Tag**（`:latest`）。
2. **服务器端固定配置**：生产服务器节点的环境配置（`compose.yaml` / `.env`）统一固定引用 `:latest` 镜像。发布新版本后，服务器端直接 `docker pull` 最新镜像并无缝重启，无需修改配置文件。

---

## 2. 三大组件发布入口

请根据需要发布的组件，参阅对应的详细指南子文档：

- 🚀 **Sidecar 旁路网关发布** ➔ 参考指南：[references/sidecar-release.md](file:///home/zikun/code/common/uestcradar/.agents/skills/docker-release/references/sidecar-release.md)
- ⚙️ **Cascade Worker 算子发布** ➔ 参考指南：[references/worker-release.md](file:///home/zikun/code/common/uestcradar/.agents/skills/docker-release/references/worker-release.md)
- 📊 **Telemetry Web 监控发布** ➔ 参考指南：[references/web-release.md](file:///home/zikun/code/common/uestcradar/.agents/skills/docker-release/references/web-release.md)
