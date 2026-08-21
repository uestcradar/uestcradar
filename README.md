# Matlab_Helium

面向雷达前端预处理 `.mat` 数据的 MATLAB 批处理工程。基于**多波位 TWS 模式**，包含逐波位 LUT 查表测角、跨波位点迹融合，以及 **EKF + GNN 多目标跟踪**。

## 项目定位

唯一正式入口：[`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m)

所有路径、开关、算法参数集中在脚本最前面的参数区，按顺序调度完整流程。

## 数据格式

前端工具（`cyhd_internal.save_bin_data_mat`）将原始 `.bin` 文件预处理为单个 `.mat` 文件，每个文件对应一次连续采集。

```text
.mat 内部结构（变量名: data）：
  data.sample_rate        = 30.72e6
  data.sample_count       = 总采样点数
  data.samples_per_frame  = 4096（每 XDMA 帧）
  data.ch0 / ch1 / ch2    = [sample_count×1] complex single，时间连续流
  data.beam.sweep_count   = [N_frames×1] uint8，扫描周期编号
  data.beam.az_code       = [N_frames×1] uint16（0~2000 → -50°~50°）
  data.beam.el_code       = [N_frames×1] uint16（0~2000 → -50°~50°）
```

每帧 4 个 PRI，PRI 长度 = 1024 采样。发射参考波形沿用原有的 `lfm_tx.bin` + `metadata.json`。

## 多波位 TWS 模式

19 波位扫描，波位角度从 -45° 到 +45° 方位。按波位分组独立 RD，最后跨波位融合：

- **旁瓣鬼影剔除**：同 (R,V) 位置、功率差 > 6dB → 剔除弱者为旁瓣泄漏
- **邻域加权融合**：波位交叠区同一目标被多次检出 → 功率加权合并
- **网格 DBSCAN**：最终空间聚类

## 波位排布

波位排布由帧内嵌的波位元数据自动提取（[`build_beam_schedule_from_meta.m`](./src/build_beam_schedule_from_meta.m)），无需外部波位文件。

元数据从每个 PRI 的 `az_code`/`el_code` 解码角度，按方位角序列识别 19 波位扫描模式，自动计算驻留脉冲数和扫描周期。

## 工程结构

### `apps/`

- [`run_batch_pipeline.m`](./apps/run_batch_pipeline.m) — 唯一正式入口

### `src/`

| 文件 | 作用 |
|---|---|
| `load_frontend_mat.m` | 前端 .mat → parse_bundle 适配层（懒加载元数据、角度解码、PRI 扩展） |
| `build_beam_schedule_from_meta.m` | 从波位元数据自动构建波位排布 |
| `preprocess.m` | 预处理入口（init / chunk），含直达波对齐、距离压缩、频偏补偿 |
| `align_direct_wave_range.m` | 直达波定位与距离零点校准 |
| `process_rd_beam.m` | 波位模式 RD：按偏移量跳读，每驻留独立 CPI，含零多普勒清除 |
| `cfar_2d.m` | 2D CA-CFAR 检测 |
| `dbscan_cluster.m` | 逐帧 DBSCAN 聚类（像素空间） |
| `mono_angle.m` | 测角统一入口：LUT 生成 / LUT 查表 + 2D 解耦（线性 k_mono 仅保留给 GUI 诊断） |
| `fuse_beam_plots.m` | 三级跨波位融合（旁瓣抑制 → 邻域加权 → 网格 DBSCAN） |
| `track_init.m` | 航迹管理器初始化，定义航迹结构体 |
| `tracker_3D_EKF.m` | 6D 笛卡尔 EKF：CV 模型 + GNN 数据关联 + M/N 航迹管理 |

### `temp_gui/`

- GUI 临时归档，不参与正式流程

### `docs/`

- [`usage_guide.md`](./docs/usage_guide.md) — 详细使用说明

## 主流程

```
参数配置 → 选择前端 .mat → load_frontend_mat 适配 → 波位排布自动构建 → 构建上下文
    → preprocess init → 逐波位 process_rd_beam → 零多普勒清除 → 检测 → 聚类
    → LUT 测角 → 跨波位融合 → EKF 多目标跟踪 → 3D 航迹 GIF
```

## 关键参数速查

```matlab
% 运行开关
cfg.run.do_process = true;           % 是否执行 RD 处理（false 则搜索已有结果）
cfg.run.do_detect = true;            % 是否执行检测+聚类+测角+融合
cfg.run.do_angle = true;             % 是否执行测角

% 测角
cfg.angle.use_lut = true;            % 是否启用 LUT 查表 + 2D 解耦测角
cfg.angle.k_az = 25.0;               % 方位单脉冲斜率
cfg.angle.k_el = 25.0;               % 俯仰单脉冲斜率

% RD
cfg.rd.n_cpi = 256;                  % CPI 脉冲数（由波位排布自动覆写）
cfg.rd.max_range_m = 2000;           % 最大处理距离 (m)
cfg.rd.zero_doppler_cells = 1;       % 零多普勒清除半宽度；RD 后 DC±N 格置零，0=仅 DC

% 路径
cfg.paths.rx_pattern = '*_data_seg*.mat';  % 前端 .mat 文件匹配模式
cfg.paths.frontend_mat_file = '';          % 留空则弹窗选择

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

每次运行在 `数据目录/Results/时间戳/` 下生成：

| 文件 | 说明 |
|---|---|
| `beam_XXX/RD_Proc_beamXXX_*.mat` | 逐波位 RD 结果 |
| `Fused_Targets_*.mat` | 融合后的全局目标列表 |
| `Tracks_*.mat` | EKF 跟踪最终航迹状态 |
| `Timeline_*.gif` | 逐帧目标动图（速度-距离，颜色=方位角） |
| `Tracking_3D_*.gif` | 3D 航迹动图（笛卡尔空间） |

## 运行方式

1. 将前端生成的 `*_data_seg*.mat` 文件放入数据目录（如 `F:\0811\mid\`）
2. 确保 TX 参考目录包含 `lfm_tx.bin` 和 `metadata.json`
3. 打开 [`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m)
4. 修改 `data_dir`、`tx_dir` 和算法参数
5. 运行；若未指定 `frontend_mat_file` 则弹窗选择 .mat 文件
