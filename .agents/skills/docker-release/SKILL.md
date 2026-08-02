---
name: docker-release
description: 在 ARM64 开发机上验证、构建并双 Tag 发布 Sidecar、Worker 和 Web 镜像到 registry.chengyistudio.com/cxx。
---

# Docker Release

本 Skill 只发布三类运行镜像：`sidecar`、`worker` 和 `web`。本机脚本仅负责
校验 Git revision 与 SSH 调度；构建、测试和推送必须在 ARM64 开发机上完成。

## 发布红线

1. 默认发布主机为 `root@192.162.2.64`，只使用 SSH Key/Agent，不接收或保存密码。
2. 必须通过 `--remote-dir` 指定远端已有仓库；远端工作树必须干净且 HEAD 与本地一致。
3. Worker 正式发布入口必须是 `workspace/examples/<worker-name>/Dockerfile`；本地交互菜单
   只列出该目录下包含 Dockerfile 的直接子目录。
4. `workspace/sidecar/Dockerfile` 的 `cascade-worker` target 只用于本地测试，不得发布。
5. 不可变 Tag 不能覆盖；先推不可变 Tag、拉回验签，再更新滚动 Tag。
6. `registry.chengyistudio.com/cxx/algo-base:latest` 是默认 Worker 开发基础镜像；SDK
   升级验证必须用 `ALGO_BASE` 固定候选 Tag 或 Digest，禁止混用 SDK ABI。

## 快速入口

在仓库根目录执行：

```bash
./.agents/skills/docker-release/scripts/release.sh \
  --remote-dir /root/workspace/uestcradar
```

脚本启动后交互选择 Sidecar、Web 或 Worker。选择 Worker 时继续选择
`workspace/examples/` 下的具体目录；Dockerfile 缺失最小 Worker Labels、构建失败或
镜像契约验证失败时立即停止，不推送镜像。

可通过 `RELEASE_HOST`、`RELEASE_USER`、`RELEASE_DIR`、`REGISTRY` 和 `ALGO_BASE`
覆盖默认值。

## 参考文档

- [极简镜像契约](references/image-contract.md)
- [Sidecar 发布](references/sidecar-release.md)
- [Worker 发布](references/worker-release.md)
- [Web 发布](references/web-release.md)
