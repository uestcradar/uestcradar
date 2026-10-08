# 单机阶段验收与范围收尾

2026-10-07。**frontend-runtime 单机阶段完成；整体目标未完成，仍需 G01 的 Web 集成和真实服务器验收。**

| 规格项 | 实际结果 |
|---|---|
| F01 | KT2/KT3 均为根目录单一 Compose/project，旧四份配置删除；修正镜像后还各回归一次默认 up -d --no-build，Sink 产生有效结果即结束，没有重复 60 秒/隔离验收 |
| F02 | KT2 真实 1:3→2:2；KT3 真实 2:2→3:2，原 Sink 检查通过；未把反量化开发模板当作已发布真实脉压镜像的源码证明 |
| F03 | KT2 输入/输出各 59 个不同 frame_id；KT3 各 12 个，观察均超过 64 秒，实际 Chrome canvas 绘图 |
| F04 | 全部节点页面身份/Leg/类型正确；畸形、错误节点/实例/类型测试通过；过期或断开明确显示，UI 时钟误判已修复 |
| F05 | 停 Frontend 30 秒，KT2 Sink 210688→218048，KT3 49→55；恢复分别约 0.49 秒收到预览、2.95 秒浏览器绘图；主链容器 ID/StartedAt 不变；浏览器关闭和慢消费者也不阻塞主链 |
| F06 | ARM 应用测试实际验证根路径/代理前缀下的静态 JS/CSS、API、WS 以及跨 Origin 拒绝；没有部署/SSH接口或 Docker Socket，HTTP 默认 loopback。真正 Web 会话代理的服务器验收仍属于 G01，未宣称已通过 |
| F07 | ARM 构建测试→Harbor→本机固定 digest→no-build 运行；全部运行镜像为 ARM64，单机明确 functional/tcp,self，无数据面静默回退 |
| F08 | Go/Vitest/UI build/vet 通过，Go 两包重复 30 次与 race 通过；现有 RD Sink/Worker CTest 2/2；保护范围与文档检查通过 |

详细证据：[发布](frontend-release.md)、[KT2](kt2-local.md)、[KT3](kt3-local.md)。最终默认启动回归：[KT2](kt2-local/final-start.txt)、[KT3](kt3-local/final-start.txt)。这些回归是镜像纠错后的启动检查，不是第二轮图像/隔离验收。

## 范围与状态

- 起点仍使用原 F00 的 `6644399` 与 `/tmp/frontend-split.RJ9R7H/` 记录，未重建保护基线。
- 功能改动仅在 `infra/frontend/` 及两个案例 Compose；案例 README 更新部署方法，规格/任务/证据同步。
- 未改案例算法、测试、数据、CMake、Dockerfile，也未改 Sidecar、SDK、Ring、协议或公共发布脚本。
- GFKD 与四份 CSV 的起点哈希全部复核通过，运行镜像中的算法产物也对应现有文件。
- 两个测试 project 均已清理，原有本机 11 个运行容器集合完全不变。未修改 binfmt、宿主 Docker 配置或硬件设置。
- Web 代码及服务器业务部署尚未改动；不把应用实现/本机成功冒充 Web 内嵌成功。

## 新鲜度回归检查

原缺陷在真实 KT2 页面 30 次采样中 24 次把刚到达帧标为过期；修复仅在收到帧时更新 UI now，未放宽 3 秒阈值。运行 KT2、待双 Leg 持续更新后，可在浏览器控制台运行以下检查（直接检查组件 DOM，不另建测试框架）：

```javascript
for (let i = 0; i < 30; i++) {
  const frames = [...document.querySelectorAll('[data-frame-id]')];
  if (frames.length !== 2 || frames.some(e => e.dataset.fresh !== 'true')) {
    throw new Error('Active KT2 frames must not be marked stale');
  }
  await new Promise(resolve => setTimeout(resolve, 50));
}
```

## 下一阶段门槛

目前没有明确硬件阻塞。`.64` 的 hns_1 已 ACTIVE/LinkUp，10 Gb/s；hns_0 未连接，不将未选中的端口或旧 Docker CLI 当作硬件失败。实际跨服务器链路仍未验证。

按既定 G01 门槛，先审定 Web 集成规格及目标服务器，再细化实现任务和部署；需要重启/替换既有工作负载或改变宿主设置时仍须明确授权。
