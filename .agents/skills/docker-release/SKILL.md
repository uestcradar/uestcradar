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
3. Worker 正式发布入口必须位于 `workspace/examples/`。当前唯一入口为
   `workspace/examples/cascade_worker/Dockerfile`。
4. `workspace/sidecar/Dockerfile` 的 `cascade-worker` target 只用于本地测试，不得发布。
5. 不可变 Tag 不能覆盖；先推不可变 Tag、拉回验签，再更新滚动 Tag。
6. `registry.chengyistudio.com/cxx/algo-base:latest` 是 Worker 开发基础镜像，保持不变。

## 快速入口

在仓库根目录执行：

```bash
./.agents/skills/docker-release/scripts/release.sh \
  --remote-dir /root/workspace/uestcradar \
  --component all
```

可通过 `RELEASE_HOST`、`RELEASE_USER`、`RELEASE_DIR` 和 `REGISTRY` 覆盖默认值。

## 参考文档

- [极简镜像契约](references/image-contract.md)
- [Sidecar 发布](references/sidecar-release.md)
- [Worker 发布](references/worker-release.md)
- [Web 发布](references/web-release.md)
