# KT2：IQ → 脉压 → 校验

## 单机部署

在本目录执行唯一启动命令：

```bash
docker compose up -d --no-build
```

`compose.yaml` 在一个 project 中启动 Source、三个 Sidecar、脉压 Worker、Sink 与三个独立 Frontend。默认使用固定 ARM64 Harbor digest、`functional / tcp,self`，不启动 Web/Nginx，不需要 `.env`、覆盖文件或现场构建。缺失镜像由 Compose 拉取，拉取失败即停止。

前提：Docker Compose 可用、Harbor 可访问；x86_64 主机已有可工作的 ARM64 binfmt/QEMU。主机环境准备不是日常部署步骤，不在启动命令里自动提权或安装模拟器。

| 页面 | 地址 |
|---|---|
| Source 输出 IQ | http://127.0.0.1:8081 |
| 脉压输入/输出 | http://127.0.0.1:8082 |
| Sink 输入 | http://127.0.0.1:8083 |

预览与主链独立；关闭页面或停止 Frontend 不应停止算法。Worker 通过 `ipc: service:对应Sidecar` 共享 IPC；保留 Sidecar SHM 配额、健康检查和启动依赖。

```bash
docker compose ps
docker compose logs -f signalsource pulsecompression signalsink
docker compose down
```

KT2 与 KT3 复用本机端口，不同时启动。已有同名 project 或端口占用时先确认，勿直接覆盖其他工作负载。QEMU 只用于功能验证，不作为原生 ARM 性能基准。

## 结果判定

页面应显示真实 IQ `1:3` → 脉压 `2:2`，输入横轴为采样点，输出横轴为距离 Bin。服务健康不等于已有有效帧；断开或过期状态必须区分。

默认发布镜像 `pulsecompression-v1.0.0` 产生每 CPI 64 个脉压帧；原 Sink 保留目标门控，正常结果包括：

```text
[sink] target_summary ... pulses=64 detected=64 missed=0 status=PASS
```

需要原有 640 脉冲严格校验时，先停止常驻 Sink，避免两个消费者读同一 Ring：

```bash
docker compose stop signalsink
docker compose run --rm --no-deps signalsink \
  --frames 640 --log-every 64 \
  --target-range 20480 --target-half-width 8 \
  --target-min-snr-db 10 --pulses-per-cpi 64 \
  --fail-on-target-miss
docker compose up -d --no-build signalsink
```

## 算法开发模板（不是日常部署步骤）

本目录 `src/` 仍是原反量化开发模板，不是上述已发布脉压镜像的源码证明。模板将完整 CPI 的 CS16 按 `dequantization_scale` 转成 ComplexFloat32，并检查 CPI0–CPI9 的元数据、64 元素脉冲参数、CS16 和循环顺序；它不执行脉冲压缩，输出 `pulses_per_cpi=1`，Sink 会标记 `target_validation=SKIPPED`，不能冒充默认镜像的 64 脉冲门控通过。

开发接口与边界保持不变：

- 使用 SDK `Input<IQFrame>` / `Output<PulseCompressionFrame>`，见 [SDK 接口指南](../../infra/sdk/README.md)。真实算法按 `pulse_time_offset_s` 与采样率确定脉冲窗口。
- 输出帧（含 Metadata 与矩阵）不得超过 32 MiB；当前模板输出 6,009,672 字节。
- 保留 Dockerfile 的官方 algo-base、Worker 契约 Label、ARM64 平台以及 Compose 的 IPC/SHM/健康依赖。
- 算法、测试、数据与 Dockerfile 未因 Frontend 拆分修改。

修改算法后，在 ARM 构建机通过原测试并按 [既有发布流程](../../../.agents/skills/docker-release/SKILL.md) 发布新的不可变 Worker 版本，再更新 Compose 中对应 digest；不使用本地 `:dev` 回退，不在部署端构建，也不覆盖已发布版本。
