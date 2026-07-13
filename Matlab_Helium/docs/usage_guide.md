# 使用说明

本文只说明当前正在使用的代码结构、原始数据目录规则、主流程、参数区和输出结果，不讨论旧入口、旧兼容逻辑或历史版本。

## 1. 当前唯一正式入口

- [`../apps/run_batch_pipeline.m`](../apps/run_batch_pipeline.m)

这个脚本是整个工程的总入口，也是你后续最优先修改的文件。当前设计原则是：

1. 所有路径、开关和算法参数都放在脚本最前面的参数区
2. 主程序从上到下直接展示完整流程
3. `src/` 里的函数只负责单一处理环节，不再承担总调度职责

## 2. 原始数据目录规则

当前代码假定每个数据集目录满足如下结构：

```text
数据集目录/
├─ TX/
│  └─ 某发射配置子目录/
│     ├─ lfm_tx.bin
│     └─ metadata.json
└─ RX/
   ├─ 2026-06-10_02-59-55/
   │  ├─ cpi_000.bin
   │  ├─ ...
   │  ├─ cpi_019.bin
   │  └─ metadata.json
   ├─ 2026-06-10_03-00-47/
   │  ├─ cpi_000.bin
   │  └─ metadata.json
   └─ ...
```

### 2.1 TX 读取规则

程序会：

1. 进入 `TX/`
2. 扫描其下的发射配置子目录
3. 选取第一个同时包含 `lfm_tx.bin` 和 `metadata.json` 的子目录

这样做的原因是：

- 你的 `TX/` 下不是直接放发射文件，而是先分一层配置目录
- 入口只需要知道这层规则，不需要把具体配置目录名写死

### 2.2 RX 读取规则

程序会：

1. 进入 `RX/`
2. 按批次子目录名称排序
3. 在每个批次目录中收集 `cpi_*.bin`
4. 按文件编号顺序拼接全部批次

这样做的原因是：

- 同一组实验数据可能分批采集
- 后续处理希望把这些批次按时间顺序视作同一条连续数据流

### 2.3 元数据读取规则

当前约定：

- 发射端元数据来自 `TX/<配置子目录>/metadata.json`
- 接收端元数据默认取 `RX/` 下第一个批次目录中的 `metadata.json`

这样做的前提是：

- 当前各接收批次的通道配置保持一致

如果未来不同批次的接收参数不一致，就需要单独改这部分逻辑。

## 3. 主流程说明

[`run_batch_pipeline.m`](../apps/run_batch_pipeline.m) 当前按下面顺序执行。

### 3.1 定位原始输入

作用：

- 定位发射参考波形
- 定位发射端元数据
- 定位接收端元数据
- 收集所有接收批次中的 `cpi_*.bin`

对应位置：

- `run_batch_pipeline.m` 内部本地函数 `locate_raw_inputs`

### 3.2 解析原始数据

作用：

- 读取发射参考波形
- 读取发射与接收元数据
- 将多通道交织的接收原始数据拆分为各通道缓存
- 生成 `rx_ch*.mat`
- 生成 `parse_info_*.mat`

为什么保留这一层：

- 原始 `bin` 解析耗时且数据量大
- 一旦解析完成，后续调 RD、检测、测角时可以直接复用

对应模块：

- [`../src/batch_parse_bin.m`](../src/batch_parse_bin.m)

### 3.3 构建 RD 上下文

作用：

- 统一计算采样率、PRI、PRF
- 统一计算距离轴和速度轴
- 统一准备 CPI、重叠、分块等公共参数
- 统一准备匹配滤波参考

为什么单独保留：

- 这些量会被预处理、RD 和绘图共同使用
- 统一构建后可以减少重复计算和参数分散

对应位置：

- `run_batch_pipeline.m` 内部本地函数 `build_rd_context`

### 3.4 初始化预处理

作用：

- 完成只需要做一次的预处理准备
- 完成直达波定位
- 初始化后续分块处理要复用的状态量

为什么先做 `init`：

- 这里不是一次性把全部数据都预处理完
- 它只负责一次性的准备动作
- 真正的大批量预处理仍然在后续 RD 分块处理中执行

这样设计的目的是：

- 避免把全部原始数据一次性读入内存
- 保证所有分块沿用同一套初始化结果

对应模块：

- [`../src/preprocess.m`](../src/preprocess.m)
- [`../src/align_direct_wave_range.m`](../src/align_direct_wave_range.m)

### 3.5 执行 RD 处理

作用：

- 按通道读取 `rx_ch*.mat`
- 按 chunk 分块读取与处理数据
- 在每个 chunk 内执行预处理
- 做距离压缩和慢时间 FFT
- 输出 `RD_Proc_*.mat`

为什么单独做成主处理模块：

- RD 是整条链路中最重的主体计算部分
- 抽出去后，入口脚本仍然能保持流程清楚

对应模块：

- [`../src/process_rd.m`](../src/process_rd.m)

### 3.6 执行目标检测

作用：

- 从 RD 结果中裁剪目标分析窗口
- 对窗口内功率图执行二维 CFAR 检测

为什么这样做：

- 先裁剪再检测，可以减少无关区域计算量
- 也能让后续聚类和测角更聚焦目标区间

对应模块：

- [`../src/cfar_2d.m`](../src/cfar_2d.m)

### 3.7 执行聚类

作用：

- 对 CFAR 输出的离散检测点做 DBSCAN 聚类

为什么单独保留：

- 一个真实目标通常会对应多个相邻检测点
- 聚类后，后续分析可以从“点级”转到“目标级”

对应模块：

- [`../src/dbscan_cluster.m`](../src/dbscan_cluster.m)

### 3.8 执行测角

作用：

- 从每个目标簇中选代表点
- 基于方位差与俯仰差通道做单脉冲测角

为什么放在聚类之后：

- 测角更适合面向“目标簇”而不是面向“离散检测点”

对应模块：

- [`../src/mono_angle.m`](../src/mono_angle.m)

### 3.9 保存分析结果

作用：

- 保存检测结果 `*_det.mat`
- 保存测角结果 `*_angle.mat`

为什么保留这些结果文件：

- 以后如果只改绘图方式、标注方式或筛选条件
- 可以直接复用分析结果，而不必重复前面的 RD 与检测过程

### 3.10 绘图与导出

作用：

- 导出 RD 动图
- 导出检测动图
- 导出测角动图

为什么单独放在最后：

- 绘图属于结果消费层
- 这样后续改显示风格时，不会影响前面的算法主流程

对应模块：

- [`../src/radar_plot.m`](../src/radar_plot.m)

## 4. 参数区说明

当前所有可编辑参数都集中在 [`run_batch_pipeline.m`](../apps/run_batch_pipeline.m) 最前面。

### 4.1 路径参数 `cfg.paths`

主要字段：

- `cfg.paths.data_folders`
- `cfg.paths.tx_root_dir`
- `cfg.paths.tx_subdir_pattern`
- `cfg.paths.tx_file_name`
- `cfg.paths.tx_meta_name`
- `cfg.paths.rx_root_dir`
- `cfg.paths.rx_batch_pattern`
- `cfg.paths.rx_meta_name`
- `cfg.paths.rx_pattern`
- `cfg.paths.parse_info_pattern`
- `cfg.paths.result_dir_name`
- `cfg.paths.rd_pattern`

建议：

- 只要原始目录结构变了，优先改这一组
- 这样可以尽量不动后面的算法逻辑

### 4.2 运行开关 `cfg.run`

主要字段：

- `cfg.run.do_parse`
- `cfg.run.do_process`
- `cfg.run.do_detect`
- `cfg.run.do_cluster`
- `cfg.run.do_angle`
- `cfg.run.do_plot`

作用：

- 决定主流程执行到哪一步

注意：

- 如果关闭某一步，必须保证前一步需要复用的结果文件已经存在

### 4.3 预处理参数 `cfg.preprocess`

主要字段：

- `cfg.preprocess.do_dw_calibrate`
- `cfg.preprocess.dw_bin_manual`
- `cfg.preprocess.do_dw_blank`
- `cfg.preprocess.do_subsample_align`
- `cfg.preprocess.do_freq_comp`
- `cfg.preprocess.do_fast_dc_remove`
- `cfg.preprocess.n_guard`

作用：

- 控制直达波对齐、直达波抑制、频偏补偿、快时间去均值等步骤

### 4.4 雷达常量 `cfg.radar`

主要字段：

- `cfg.radar.c`
- `cfg.radar.fc`

说明：

- `c` 是光速
- `fc` 是载频

当前其他参数如采样率、PRI 等，不在这里手填，而是在解析阶段从 `metadata.json` 自动读取。

### 4.5 RD 参数 `cfg.rd`

主要字段：

- `cfg.rd.n_cpi`
- `cfg.rd.n_overlap`
- `cfg.rd.max_range_m`
- `cfg.rd.frames_per_chunk`
- `cfg.rd.do_mti_twopulse`

作用：

- 控制 CPI 长度、块间重叠、最大处理距离、按块读取规模和 MTI 处理方式

### 4.6 检测参数 `cfg.detect`

主要字段：

- `cfg.detect.range_window_m`
- `cfg.detect.velocity_window_mps`
- `cfg.detect.cfar_guard_r`
- `cfg.detect.cfar_guard_d`
- `cfg.detect.cfar_ref_r`
- `cfg.detect.cfar_ref_d`
- `cfg.detect.cfar_pfa`

当前特别说明：

- `cfg.detect.cfar_pfa = 1e-6`

这意味着当前检测策略更偏保守，用意是进一步压低虚警数量。

### 4.7 聚类参数 `cfg.cluster`

主要字段：

- `cfg.cluster.dbscan_eps`
- `cfg.cluster.dbscan_min`

### 4.8 测角参数 `cfg.angle`

主要字段：

- `cfg.angle.k_mono`
- `cfg.angle.range_window_m`
- `cfg.angle.velocity_window_mps`
- `cfg.angle.min_display_power_dB`

### 4.9 导出参数 `cfg.export`

主要字段：

- `cfg.export.save_analysis_mat`
- `cfg.export.gif_delay`
- `cfg.export.keep_parse_mat`
- `cfg.export.keep_rd_mat`

说明：

- `keep_parse_mat` 决定是否保留解析缓存
- `keep_rd_mat` 决定是否保留 RD 主结果

### 4.10 绘图参数 `cfg.plot`

主要字段：

- `cfg.plot.range_window_m`
- `cfg.plot.velocity_window_mps`
- `cfg.plot.clim_dB`
- `cfg.plot.frame_step`

作用：

- 控制绘图窗口范围、色条范围和 GIF 抽帧步长

## 5. 输出结果说明

每次运行都会在数据集目录下生成：

```text
数据集目录/Results/时间戳/
```

可能输出的文件包括：

### 5.1 解析阶段

- `rx_ch*.mat`
  - 各接收通道的解析缓存
- `parse_info_*.mat`
  - 解析索引文件，记录：
  - 本次解析时间戳
  - 接收参数
  - 发射参考结构
  - 对应的原始 `cpi_*.bin` 文件列表

### 5.2 RD 阶段

- `RD_Proc_*.mat`
  - RD 主结果文件

### 5.3 目标分析阶段

- `*_det.mat`
  - 检测与聚类结果
- `*_angle.mat`
  - 测角结果

### 5.4 绘图阶段

- `*_rd.gif`
- `*_det.gif`
- `*_angle.gif`

## 6. 模块清单

当前 `src/` 模块分工如下：

- [`../src/batch_parse_bin.m`](../src/batch_parse_bin.m)
  - 原始数据解析
- [`../src/preprocess.m`](../src/preprocess.m)
  - 预处理入口
- [`../src/align_direct_wave_range.m`](../src/align_direct_wave_range.m)
  - 直达波定位与距离零点校准
- [`../src/process_rd.m`](../src/process_rd.m)
  - RD 主处理
- [`../src/cfar_2d.m`](../src/cfar_2d.m)
  - CFAR 检测
- [`../src/dbscan_cluster.m`](../src/dbscan_cluster.m)
  - 目标聚类
- [`../src/mono_angle.m`](../src/mono_angle.m)
  - 单脉冲测角
- [`../src/radar_plot.m`](../src/radar_plot.m)
  - GIF 绘图导出

## 7. 文档维护规则

当前建议固定如下分工：

- [`../apps/run_batch_pipeline.m`](../apps/run_batch_pipeline.m)
  - 第一参考，所有实际参数以这里为准
- [`../README.md`](../README.md)
  - 项目总体概览
- [`usage_guide.md`](./usage_guide.md)
  - 详细使用说明

以下情况发生时，建议同步更新文档：

- 原始目录结构变化
- 参数区字段变化
- 主流程顺序变化
- 模块职责变化
- 输出结果规则变化
