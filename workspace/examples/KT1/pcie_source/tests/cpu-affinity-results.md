# CPU 角色绑定修复：候选验证

状态：Source 代码已修复；用户已授权正式发布和数据流测试。本报告保留发布前候选验证；正式发布与双机验证已完成，见 [cpu-release-results.md](cpu-release-results.md)。

## 修改

- `PCIE_CPU_MAP=RX,DMA,OUTPUT`，或 `--cpu-map RX,DMA,OUTPUT`（CLI 优先），当前平台默认 8,9,10。
- 在缩小主线程 affinity 前验证原始允许集合；拒绝重复、越界或活动角色不可用的 CPU。capture-only 不要求未使用的输出 CPU 在线。
- 主线程及两个明确 native_handle 按角色绑定、读回验证；DMA helper 绑定在接收器硬件配置之前完成，输出线程在首次提交数据之前完成。
- 绑定失败清理已启动的等待线程后抛错。RawOutput 的中止通知在 mutex 保护下修改条件，避免失败清理与 wait 竞争造成丢唤醒。
- 不修改 SignalSink、SDK、Sidecar、RawIQ 契约、双快照、缺口失败或原始数据策略；不改宿主全局调度或添加权限。

## 软件回归

- 本地 17/17：包括可选仿真 Source 进程停止测试。
- ARM 原生 16/16：未配置可选仿真 Source 二进制。
- 本地 ASan+UBSan 16/16。
- 新测试覆盖严格解析、环境/CLI 优先级、线程设置与读回、继承窄 affinity 后绑定不同 CPU、重复失败后的清理、失败时未调用硬件初始化。
- 首次 ARM 源码归档遗漏 common 下的父级头文件，构建失败；补全测试输入后全部重跑通过。失败日志保留。

## 硬件测试：候选二进制，不是正式镜像验收

原生 ARM 候选挂载到旧运行镜像 `/app/pcie_source`，仅接收、无网络、无 SDK 输出或 SignalSink。没有测试脚本外部 taskset，线程绑定由修复后的程序自行完成。

候选 SHA256：`a9eefc19b87eb54f00ce102f44fa76a8bc56fb19b1ed501946dcfc158e131275`。

- `--cpu-map 8,8,10`：退出 1，`cpu-map roles require distinct CPUs`，未进入设备启动。
- 容器允许集合仅 CPU8：退出 1，`cpu-map CPU is offline or outside the allowed set`，未进入设备启动。
- `/proc/<pid>/task` 读回：主线程仅 CPU8，helper 仅 CPU9。

### 首轮失败必须保留

主机当时刚重启（随后观测 uptime 约 13 分钟）。候选首次运行只有 BIT 包，0 IQ/0 control，约 2.235 秒出现 1 个 invalid descriptor，退出 1：

```text
PCIe receive gap or unstable DMA snapshot; recording run failed
```

changed_copies=0，invalid_packets=1。这不是此前的“上一帧未完整便收到下一控制表”。该次无效描述符原因未证实，不能直接归因于重启或光纤，也不能把首轮改记成成功。

随后原版发布镜像、分核仅接收 10 秒通过（37,400 帧）。再运行候选三轮：

| 候选轮次 | 时长 | 完整帧 | 结果 |
|---|---:|---:|---|
| 1 | 60 秒 | 224,901 | 退出 0 |
| 2 | 60 秒 | 224,901 | 退出 0 |
| 3 | 60 秒 | 224,900 | 退出 0 |

三轮均 invalid_packets=0、changed_copies=0、timestamp_errors=0，未重现之前配对错误。启动丢弃的未对齐样本、停止时残帧继续单独报告，不拼接保存。

## 候选阶段尚未完成的事项（正式验证另见上述报告）

- 正式 Git 提交/同步、原生 Harbor 发布、新 digest 的 Web 批准更新。
- 固定发布镜像的正常 Web 双机 strict-RDMA 10 秒/60 秒录制，以及源/文件摘要、最终同步等验收。
- 首轮无输入后 invalid descriptor 的原因、冷启动稳定性、长期稳定性与硬件所有权保证。

因此当前结论仅是“CPU 修复候选在三轮 60 秒仅接收中未复现旧配对错误”，不是“完整业务链已修复”。测试容器已删除；原 Web 与已有录制文件保留。

原始日志及结果：`tests/evidence/raw-iq/cpu-fix/`，`sha256.json` 为完整性索引。
