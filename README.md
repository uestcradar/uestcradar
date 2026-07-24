# Matlab_Helium

面向雷达原始 `bin` 数据的 MATLAB 批处理工程。基于**多波位 TWS 模式**，包含逐波位 LUT 查表测角、跨波位点迹融合，以及 **EKF + GNN 多目标跟踪**。

## 项目定位

唯一正式入口：[`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m)

所有路径、开关、算法参数集中在脚本最前面的参数区，按顺序调度完整流程。

## 多波位 TWS 模式

每个 CPI 文件 = 一轮扫描（128 波位 × 256 脉冲）。按波位分组独立 RD，最后跨波位融合：

- **旁瓣鬼影剔除**：同 (R,V) 位置、功率差 > 6dB → 剔除弱者为旁瓣泄漏
- **邻域加权融合**：波位交叠区同一目标被多次检出 → 功率加权合并
- **网格 DBSCAN**：最终空间聚类

## 原始数据目录结构

```text
数据集目录/
├─ TX/
│  └─ 某发射配置子目录/
│     ├─ lfm_tx.bin
│     └─ metadata.json
└─ RX/
   ├─ 2026-06-10_02-59-55/
   │  ├─ cpi_000.bin  ~  cpi_039.bin
   │  └─ metadata.json
   └─ ...
```

每个 CPI 文件包含一轮完整扫描（128 波位 × 256 脉冲 = 32,768 PRI）。

## 波位排布文件格式

多波位模式需要波位文件（如 `波位格式.txt`），格式：`方位角(°), 俯仰角(°), [驻留脉冲数]`。

```text
# 注释以 # 开头
-37.5, -17.5, 256
-32.5, -17.5, 256
...
 37.5,  17.5, 256
```

## 工程结构

### `apps/`

- [`run_batch_pipeline.m`](./apps/run_batch_pipeline.m) — 唯一正式入口

### `src/`

| 文件 | 作用 |
|---|---|
| `batch_parse_bin.m` | 原始 bin 解析：交织 → 单通道 mat |
| `preprocess.m` | 预处理入口（init / chunk），含直达波对齐、距离压缩、频偏补偿 |
| `align_direct_wave_range.m` | 直达波定位与距离零点校准 |
| `process_rd_beam.m` | 波位模式 RD：按偏移量跳读，每驻留独立 CPI |
| `cfar_2d.m` | 2D CA-CFAR 检测 |
| `dbscan_cluster.m` | 逐帧 DBSCAN 聚类（像素空间） |
| `mono_angle.m` | 测角统一入口：线性 k_mono / LUT 生成 / LUT 查表 + 2D 解耦 |
| `fuse_beam_plots.m` | 三级跨波位融合（旁瓣抑制 → 邻域加权 → 网格 DBSCAN） |
| `parse_beam_schedule.m` | 波位排布文件解析 |
| `track_init.m` | 航迹管理器初始化，定义航迹结构体 |
| `tracker_3D_EKF.m` | 6D 笛卡尔 EKF：CV 模型 + GNN 数据关联 + M/N 航迹管理 |

### `temp_gui/`

- GUI 临时归档，不参与正式流程

### `docs/`

- [`usage_guide.md`](./docs/usage_guide.md) — 详细使用说明

## 主流程

```
参数配置 → 定位原始输入 → 解析 bin → 波位排布加载 → 构建上下文
    → preprocess init → 逐波位 process_rd_beam → 检测 → 聚类
    → LUT 测角 → 跨波位融合 → EKF 多目标跟踪 → 3D 航迹 GIF
```

## 关键参数速查

```matlab
% 运行开关
cfg.run.do_parse = false;            % 重新解析 bin
cfg.run.do_process = false;          % 重新 RD 处理

% 测角
cfg.angle.use_lut = true;            % LUT 查表 vs 线性 k_mono
cfg.angle.k_az = 25.0;               % 方位单脉冲斜率
cfg.angle.k_el = 25.0;               % 俯仰单脉冲斜率

% RD
cfg.rd.n_cpi = 256;                  % CPI 脉冲数（由波位文件自动覆写）
cfg.rd.max_range_m = 2000;           % 最大处理距离 (m)

% 检测
cfg.detect.range_window_m = [300, 800];
cfg.detect.velocity_window_mps = [-50, 50];

% 跟踪 (EKF + GNN)
cfg.track.enable = true;               % 启用多目标跟踪
cfg.track.decimation = 1;              % 跟踪降采样：1 = 每帧
cfg.track.q = 0.1;                     % 过程噪声强度
cfg.track.v_tan_std_init = 10.0;       % 初始切向速度不确定性 (m/s)
cfg.track.vr_noise_std = 2.0;          % 径向速度量测噪声 (m/s)
cfg.track.M = 50;                      % M/N 确认：最少命中次数
cfg.track.N = 60;                      % M/N 确认：判定窗口帧数
cfg.track.max_predictions = 3;         % 连续丢失终止阈值
```

## 多目标跟踪 (EKF + GNN)

采用 6D 笛卡尔坐标系扩展卡尔曼滤波，处理 4D 极坐标量测。

| 项目 | 定义 |
|---|---|
| 状态向量 | `[px, vx, py, vy, pz, vz]` (m, m/s) |
| 量测向量 | `[range, azimuth, elevation, range_rate]` (m, rad, rad, m/s) |
| 运动模型 | 恒速 (CV)，过程噪声 q 可调 |
| 数据关联 | 全局最近邻 (GNN)，马氏距离波门 |
| 航迹管理 | M/N 逻辑 (M=50, N=60 可配) + 连续丢失终止 |

**航迹状态机**：
```
起始 → 候选 (积累命中) → 确认 (is_confirmed=true) → 丢失 → 终止
  ↑                        ↓ M/N 失败                    ↓
  └── 未关联量测 ──────── 新航迹                   连续丢帧 > max_predictions
```

**径向速度约定**：RD 处理侧正 = 接近，EKF 侧正 = 远离。在送入 EKF 前自动翻转符号。

## 输出结果

每次运行在 `数据集目录/Results/时间戳/` 下生成：

| 文件 | 说明 |
|---|---|
| `rx_ch*.mat` | 各通道解析缓存 |
| `parse_info_*.mat` | 解析索引 |
| `beam_XXX/RD_Proc_beamXXX_*.mat` | 逐波位 RD 结果 |
| `Fused_Targets_*.mat` | 融合后的全局目标列表 |
| `Tracks_*.mat` | EKF 跟踪最终航迹状态 |
| `Timeline_*.gif` | 逐帧目标动图（速度-距离，颜色=方位角） |
| `Tracking_3D_*.gif` | 3D 航迹动图（笛卡尔空间） |

## 运行方式

1. 打开 [`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m)
2. 修改参数区的数据目录、开关和算法参数
3. 直接运行；若 `data_folders` 为空则弹窗选择目录
