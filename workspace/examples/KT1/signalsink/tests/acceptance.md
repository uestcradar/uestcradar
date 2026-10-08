# SignalSink 验证进度

2026-10-08。源码已实现，**不等于整体验收或发布完成**。

## 已验证

- 原生 ARM `.64`：SDK CTest **4/4**；SignalSink 最终 CTest **4/4**。
- SignalSink 包含默认关闭、完整原始字节/顺序、私有 CLI、过期 stop ID、路径限制、空间不足、真实短写/收尾失败、队列溢出、无输入停止/退出、大帧与高熵样本篡改检测。
- 原生 ARM：Web UI 构建及 **29** 项测试通过；Go 全包测试、vet、orchestration race 通过。
- 本地真实 Chrome、模拟控制响应：1280×700 / 1440×900 / 2000×1100 下录制区域位于 iframe 外，无抽屉溢出；关闭后不发 stop，离线禁用操作。这不是实际 SSH/容器管理验收。
- 本地混合架构功能实验：amd64 SignalSink + 仿真 ARM 发布版 Source/Sidecar/Frontend，不运行 Web；656 个 IQ 帧、1,972,607,744 原始字节完成并通过文件校验。起停期间六个测试容器身份不变，随后仅移除本次测试项目。此项**不是原生 ARM 单机闭环验收**。
- 真盘隔离测试及失败优化试验见 [吞吐诊断](throughput-results.md)。高熵目标吞吐未通过；提前回写候选已撤回。

## 保留的门槛

- T10：原生 ARM 生产 Dockerfile/SDK 镜像及完整三件套闭环仍需完成。
- T11：真实 Web → SSH → 已验证运行镜像的完整控制/隔离验收尚未完成；mock、SSH fixture 和单独 CLI 测试不替代此项。
- T12：存储持续带宽未通过，当前磁盘也不足以安全容纳 60 秒目标数据加余量。
- T13：没有新的 Harbor 发布或固定 digest 部署；没有 commit/push。
- 当前只证实 `.64` 密钥登录可用；`.16/.32/.80` 已连通但密钥认证失败，不是网络不可达。

最终原生测试及 Web 输出保存在同目录 `evidence/native-tests.txt`、`evidence/native-web.txt`。运行时构建目录 `/tmp/signalsink-native.mEPM4C` 为临时目录，重启可能丢失；版本化源码与证据才是复现依据。
