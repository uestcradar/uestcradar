# Matlab_Helium

面向雷达前端预处理 `.mat` 数据的 MATLAB 批处理工程。基于**多波位 TWS 模式**，包含逐波位实测方向图 LUT 测角、跨波位点迹融合，以及 **EKF + GNN 多目标跟踪**。

## 项目定位

唯一正式入口：[`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m)

所有路径、开关、算法参数集中在脚本最前面的参数区，按顺序调度完整流程。

## 数据格式

采集端输出**新的 v7.3 `.mat`**（HDF5），顶层直接是三个通道 + 两个结构体，**没有 `data.*` 包装层**：

```text
/param/         标量，每个文件一份
  fs            采样率 (30.72e6)
  fc            载频 (9.5e9)      ← 运行时覆盖配置区的 cfg.radar.fc
  PRF           脉冲重复频率 (30000)
  PRI           脉冲重复间隔 (s)
  tau           脉宽 (2.0833e-6)
  B             带宽 (30e6)
  CPI           CPI 脉冲数
  t0            起始 UTC 时刻 (s)

/beam/          逐帧数组，长度 = 总帧数
  az_code       uint16，方位码 → az_deg = az_code * 0.05 - 50  (0~2000 → -50°~+50°)
  el_code       uint16，俯仰码 → el_deg = el_code * 0.05 - 50
  sweep_count   uint8，扫描周期编号
  current_beam_idx  uint16，波位序号
  pulse_in_beam uint16，波位内脉冲序号
  timestamp     uint64

/ch0 /ch1 /ch2  complex single，[总采样点数×1]，时间连续流
                约定：ch0 = 和口 Σ，ch1 = 俯仰差口，ch2 = 方位差口
```

每帧 4 个 PRI，PRI 长度 = 1024 采样。发射参考沿用 `lfm_tx.bin` + `metadata.json`。

> **通道约定不要靠猜。** `ch1`/`ch2` 谁是方位差口曾搞反过并导致测角左右镜像，
> 最终由拆机确认（ch1 = 俯仰差、ch2 = 方位差）。换设备或重新接线后，
> 用"差口随航向的斜率"或实测方向图复核一次。

首次运行时 [`load_frontend_mat.m`](./src/load_frontend_mat.m) 会把三通道拆成
`<基名>_ch0.mat` / `_ch1.mat` / `_ch2.mat` 三个 flat 文件，后续运行走轻量路径直接读它们。

## 多波位 TWS 与波位排布

**39 波位 = 13 方位（−30°…+30°，步进 5°）× 3 俯仰（0° / 5° / 10°）**。

波位排布由**帧内嵌的 `/beam` 元数据自动推导**（[`build_beam_schedule_from_meta.m`](./src/build_beam_schedule_from_meta.m)），
不需要外部波位文件；扫描周期边界由 `sweep_count` + `current_beam_idx` 判定。

扫描模式由 `cfg.scan_mode` 选择：

| 模式 | 含义 |
|---|---|
| `'triangle'` | 三角往返（蛇形）：一轮扫描 = 一个方向，波位顺序逐轮交替（当前数据） |
| `'sawtooth'` | 锯齿波（单向）：一轮扫描 = 固定 39 波位顺序，扫描间跳回（后续实验） |

逐波位独立 RD，最后跨波位三级融合（[`fuse_beam_plots.m`](./src/fuse_beam_plots.m)）：

- **旁瓣鬼影抑制**：同 (R,V) 位置、功率差 > 6 dB → 剔除弱者为旁瓣泄漏
- **邻域加权融合**：波位交叠区同一目标被多次检出 → 功率加权合并
- **网格 DBSCAN**：最终空间聚类

## 测角与符号位（φ）

测角走**实测方向图 LUT**（`cfg.angle.use_measured_lut = true`），幅值与符号分工明确：

- **角度幅值**来自模值 `|Δ/Σ|` —— 对接收链路相位**完全免疫**，是可靠的那一半；
- **侧别符号**来自 `sign(real(Δ/Σ · e^(−iφ)))`，其中 **φ = 判别器相位**（`cfg.angle.phi_az` / `phi_el`）。

**为什么必须带 φ：** 单脉冲的理想复比值是**纯虚数**（`Δ/Σ = i·tan(βδ)`），侧别信息全在虚轴上。
`sign(real(·))` 等价于假设 `φ = 0`；当 `φ` 离 0° 超过 90° 时，实部只剩一个与侧别无关的残差，
表现为"左右恒定偏向一侧 / 整段航迹被镜像"。本仓库踩过这个坑。

**φ 的现行口径：按批次固定 + 每批自检，不做在线测量。**

| | 做法 |
|---|---|
| 取值 | `cfg.angle.phi_az` 固定常量（当前 `-113`），换频点 / 换硬件 / 自检报警时才重标 |
| 自检 | [`sign_selfcheck.m`](./src/sign_selfcheck.m) 每次批处理免费跑，输出 `ok` / `warn` / `fail` 一行结论 |
| 重标口径 | 见 [`docs/monopulse_sign_failure.md`](./docs/monopulse_sign_failure.md) §7.6 |

自检用的是"投影效率" `Σ|Re(c·e^(−iφ))| / Σ|c|`，与侧别无关，**不需要真值、不需要标定航线**：
`≈1` 表示 φ 对齐良好，`≪0.637`（随机相位地板）表示符号已被固定偏置主导。

**为什么不用直达波在线测：** 曾经的方案是用直达波复比值反推 φ（`src/estimate_phi_from_dw.m`）。
2026-09-30 的实测否定了它——相隔 12 天的两批数据里 `φ_az` 只动了 **11°**，
而直达波的 `arg(R_dw)_az` 动了 **96°**，二者解耦。用过期补偿量会**自信地代入一个错 φ**
（实测同号率仅 14.5%）。**该路径已从管线移除**，模块保留备查但不再接线。

完整的成因推导、实测证据与标定方法见 [`docs/monopulse_sign_failure.md`](./docs/monopulse_sign_failure.md)。

## 工程结构

### `apps/`

| 文件 | 作用 |
|---|---|
| [`run_batch_pipeline.m`](./apps/run_batch_pipeline.m) | 唯一正式入口 |
| [`convert_rtk_truth.m`](./apps/convert_rtk_truth.m) | 无人机 RTK 真值 → 雷达相对坐标（SRT → 真值 mat + 二维航迹图），独立于主链路 |
| [`replay_plots.m`](./apps/replay_plots.m) | 从保存的 `PlotData_*.mat` 重新生成三类图，无需重跑管线 |

### `src/`

| 文件 | 作用 |
|---|---|
| `load_frontend_mat.m` | 前端 .mat → parse_bundle 适配层（读 `/param`+`/beam`，首次拆分三通道） |
| `build_beam_schedule_from_meta.m` | 从帧内嵌波位元数据构建波位排布与扫描周期 |
| `preprocess.m` | 预处理入口（init / chunk），含逐通道去均值、加窗匹配滤波、直达波对齐 |
| `align_direct_wave_range.m` | 直达波定位与距离零点校准 |
| `process_rd_beam.m` | 波位模式 RD：按偏移量跳读，每驻留独立 CPI，含零多普勒清除 |
| `cfar_2d.m` | 2D CA-CFAR 检测 |
| `dbscan_cluster.m` | 逐帧 DBSCAN 聚类（像素空间） |
| `mono_angle.m` | 测角统一入口：LUT 生成（理论 / 实测方向图）/ LUT 查表 + 2D 解耦 |
| `load_ccc_pattern.m` | 解析 `.ccc` 远场方向图文件 |
| `sign_selfcheck.m` | **符号自检**：零成本判断当前 φ 是否还携带侧别信息 |
| `wrap180.m` | 角度归一到 (−180°, 180°]（**度数**；勿与收弧度的 `wrapToPi` 混用） |
| `estimate_phi_from_dw.m` | 直达波在线测 φ。**已停用、未接线**，保留备查（原因见上节） |
| `fuse_beam_plots.m` | 三级跨波位融合（旁瓣抑制 → 邻域加权 → 网格 DBSCAN） |
| `llh2radar_xyz.m` | 经纬高（椭球）→ 雷达相对直角坐标，坐标系与 `tracker_3D_EKF` 一致 |
| `track_init.m` | 航迹管理器初始化，定义航迹结构体 |
| `tracker_3D_EKF.m` | 6D 笛卡尔 EKF：CV 模型 + GNN 数据关联 + M/N 航迹管理 |
| `plot_figures.m` | 绘图统一入口（kind 分发）：二维点迹 / 二维航迹 / Timeline 动图 / 3D 航迹动图 / 逐波位 RD 热力图 |
| `plot_point_trace_2d_empty.m` | 波位排布示意图脚本（测试用，直接运行） |

### `temp_gui/`

- GUI 临时归档，不参与正式流程（其中部分回调引用了已删除的函数，仅供查阅）

### `docs/`

- [`monopulse_sign_failure.md`](./docs/monopulse_sign_failure.md) — 测角符号失效的成因、实测证据与标定口径
- [`usage_guide.md`](./docs/usage_guide.md) — ⚠ 已过期，勿参考

## 主流程

```
参数配置 → 选择数据集目录 / 前端 .mat → load_frontend_mat 适配 → 波位排布自动构建 → 构建上下文
    → preprocess init → 逐波位 process_rd_beam → 零多普勒清除 → CFAR 检测 → DBSCAN 聚类
    → LUT 测角 → 跨波位融合 → EKF 多目标跟踪 → 三维航迹 GIF → [符号自检] 结论
```

## 关键参数速查

以下为 [`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m) 的**当前值**，改参数请以该文件为准。

```matlab
% 路径：{''} = 运行时弹窗选择数据集目录
cfg.paths.data_folders = {''};

% 波位筛选：只处理落在 [min,max] 内的波位，用于单波位/单俯仰层调试
% 当前值 = 全部方位层 + 仅俯仰 5°（即 13 个波位）；放开全量请令 el 为 0~10
cfg.beam.min_azimuth = 0;   cfg.beam.max_azimuth = 30;
cfg.beam.min_elevation = 5; cfg.beam.max_elevation = 5;

% 运行开关
cfg.run.do_process = true;           % 是否执行 RD 处理
cfg.run.do_detect  = true;           % 是否执行检测+聚类+测角+融合
cfg.run.do_angle   = true;           % 是否执行测角
cfg.run.do_plot    = true;           % Timeline 动图
cfg.run.do_plot_3d = true;           % 3D 航迹动图

% 测角（幅值走模值、符号走 φ 投影）
cfg.angle.use_measured_lut = true;   % 用实测远场方向图生成鉴角 LUT（优先于理想模型）
cfg.angle.pattern_dir      = 'F:\Matlab_Helium\X256B-A24147';
cfg.angle.use_lut          = true;
cfg.angle.lut_roi_deg      = 5.0;    % LUT 覆盖 ±ROI，应 ≥ 波位间隔的一半
cfg.angle.phi_az           = -113;   % 方位判别器相位 (°)，0 = 旧的 sign(real()) 行为
cfg.angle.phi_el           = 0;      % 俯仰判别器相位 (°)，尚未标定
cfg.angle.sign_az          = 1;      % 硬件符号方向 → 偏离角正方向
cfg.angle.sign_el          = 1;

% RD
cfg.rd.n_cpi = 256;                  % 由波位排布逐波位覆写
cfg.rd.max_range_m = 800;
cfg.rd.zero_doppler_cells = -1;      % 零多普勒清除半宽度；0=仅 DC，负值=不清除

% 检测
cfg.detect.range_window_m = [200, 800];
cfg.detect.velocity_window_mps = [-50, 50];
cfg.detect.cfar_pfa = 1e-6;

% 跟踪 (EKF + GNN)
cfg.track.enable = true;
cfg.track.decimation = 1;
cfg.track.q = 0.03;
cfg.track.v_tan_std_init = 10.0;
cfg.track.vr_noise_std = 2.0;
cfg.track.M = 7;                     % M/N 确认：最少命中次数
cfg.track.N = 9;
cfg.track.max_predictions = 4;       % 连续丢失终止阈值
```

## 多目标跟踪 (EKF + GNN)

采用 6D 笛卡尔坐标系扩展卡尔曼滤波，处理 4D 极坐标量测。

| 项目 | 定义 |
|---|---|
| 状态向量 | `[px, vx, py, vy, pz, vz]` (m, m/s) |
| 量测向量 | `[range, azimuth, elevation, range_rate]` (m, rad, rad, m/s) |
| 运动模型 | 恒速 (CV)，过程噪声 q 可调 |
| 数据关联 | 全局最近邻 (GNN)，马氏距离波门 |
| 航迹管理 | M/N 逻辑 (M=7, N=9 可配) + 连续丢失终止 |

**航迹状态机**：

```
起始 → 候选 (积累命中) → 确认 (is_confirmed=true) → 丢失 → 终止
  ↑                        ↓ M/N 失败                    ↓
  └── 未关联量测 ──────── 新航迹                   连续丢帧 > max_predictions
```

**径向速度约定**：RD 处理侧正 = 接近，EKF 侧正 = 远离。在送入 EKF 前自动翻转符号。

## 输出结果

每次运行在 `数据目录/Results/<时间戳>/<批次名>/` 下生成：

| 文件 | 说明 |
|---|---|
| `beam_XXX/RD_Proc_beamXXX_*.mat` | 逐波位 RD 结果（`cfg.beam.output_rd_per_beam` 控制是否保留） |
| `Fused_Targets_*.mat` | 融合后的全局目标列表 |
| `Tracks_*.mat` | EKF 跟踪最终航迹状态 |
| `Point_Trace_2D_*.png` | 二维点迹图（笛卡尔地面投影） |
| `Track_Map_2D_*.png` | 二维航迹图（笛卡尔地面投影） |
| `Timeline_*.gif` | 逐帧目标动图（速度-距离，颜色 = 方位角） |
| `Tracking_3D_*.gif` | 3D 航迹动图（笛卡尔空间） |
| `*_raw_rd.gif` / `*_raw_rd.fig` | 逐波位原始 RD 热力图 |
| `PlotData_*.mat` | 绘图数据存档（供 `apps/replay_plots.m` 复现三类图） |

控制台另会打印一行 **`[符号自检]`** 结论，说明本批 φ 是否还携带侧别信息。

## 运行方式

1. 确认数据目录结构为 `数据集目录/TX/<发射配置>/lfm_tx.bin + metadata.json` 与 `数据集目录/RX/`（放前端 `.mat`）
2. 打开 [`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m)
3. `cfg.paths.data_folders` 留空 `{''}` 则运行时弹窗选数据集目录；也可直接填绝对路径批处理多个
4. `cfg.paths.frontend_mat_file` 留空则弹窗选前端 `.mat`
5. 运行

## 注意事项

- **通道约定、采集配置这类硬件事实，以拆机结论或记录为准，不要从数据反推。**
- `X256B-A24147/` 下的远场 `.ccc`：和口与差口是**分不同会话**测的，
  其 Σ/Δ **相对相位不可信**（幅度可信）。不要用它推 φ。
- `cfg.angle.phi_el` 仍为 0（**未标定**）：本批数据 `δel` 全程不过零，无法约束。
  要标定需要一条俯仰剖面的飞行。
- 换频点、换硬件批次、或 `[符号自检]` 报 `warn`/`fail` 时，需重标 `cfg.angle.phi_az`。
