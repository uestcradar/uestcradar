# Matlab_Helium

Matlab_Helium 是一个面向雷达原始 `bin` 数据的 MATLAB 批处理工程。当前版本强调三件事：

- 入口脚本直接展示完整处理顺序
- `src/` 只保留真正参与当前流程的处理模块
- 所有可修改参数集中放在入口脚本最前面的参数区

## 项目定位

当前唯一正式入口：

- [`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m)

这个入口脚本承担两类职责：

1. 在顶部统一放置全部路径、开关和算法参数
2. 从上到下依次调度完整流程：数据定位、解析、预处理、RD、检测、聚类、测角、保存、绘图

## 原始数据目录结构

当前代码按如下结构读取数据：

```text
数据集目录/
├─ TX/
│  └─ 某发射配置子目录/
│     ├─ lfm_tx.bin
│     └─ metadata.json
└─ RX/
   ├─ 某采集批次子目录/
   │  ├─ cpi_000.bin
   │  ├─ ...
   │  ├─ cpi_019.bin
   │  └─ metadata.json
   ├─ 某采集批次子目录/
   │  ├─ cpi_000.bin
   │  └─ metadata.json
   └─ ...
```

当前读取规则如下：

1. 先进入数据集目录下的 `TX/`
2. 在 `TX/` 下寻找一个同时包含 `lfm_tx.bin` 和 `metadata.json` 的发射配置子目录
3. 再进入数据集目录下的 `RX/`
4. 按批次子目录名称排序，依次收集每个批次中的 `cpi_*.bin`
5. 默认使用第一个接收批次中的 `metadata.json` 作为接收参数来源

这样设计的目的，是直接贴合你当前真实的落盘方式，避免再靠旧目录兼容逻辑增加中间层。

## 工程结构

### `apps/`

- [`run_batch_pipeline.m`](./apps/run_batch_pipeline.m)
  - 当前唯一正式入口

### `src/`

- [`batch_parse_bin.m`](./src/batch_parse_bin.m)
  - 解析原始 `bin` 数据，输出通道缓存和解析索引
- [`preprocess.m`](./src/preprocess.m)
  - 预处理入口，负责初始化和分块预处理
- [`align_direct_wave_range.m`](./src/align_direct_wave_range.m)
  - 直达波定位与距离零点校准
- [`process_rd.m`](./src/process_rd.m)
  - 完成 RD 主处理
- [`cfar_2d.m`](./src/cfar_2d.m)
  - 二维 CFAR 检测
- [`dbscan_cluster.m`](./src/dbscan_cluster.m)
  - 检测点聚类
- [`mono_angle.m`](./src/mono_angle.m)
  - 单脉冲测角
- [`radar_plot.m`](./src/radar_plot.m)
  - 基于已有结果做 GIF 绘图导出

### `docs/`

- [`usage_guide.md`](./docs/usage_guide.md)
  - 当前详细使用说明

### `temp_gui/`

- GUI 临时归档，不参与当前正式主流程

## 当前主流程

[`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m) 当前按以下顺序执行：

1. 设置路径参数、运行开关和算法参数
2. 按 `TX/` 与 `RX/` 目录规则定位原始输入
3. 解析 `cpi_*.bin`
4. 构建 RD 共用上下文
5. 初始化预处理状态
6. 执行 RD 处理
7. 执行 CFAR 检测
8. 执行 DBSCAN 聚类
9. 执行单脉冲测角
10. 保存分析结果
11. 导出 GIF 图像

## 输出结果

每次运行都会在数据集目录下创建：

```text
数据集目录/Results/时间戳/
```

当前可能输出的结果包括：

- `rx_ch*.mat`
  - 各接收通道解析缓存
- `parse_info_*.mat`
  - 解析索引文件，记录本次解析参数与对应缓存文件
- `RD_Proc_*.mat`
  - RD 主结果
- `*_det.mat`
  - 检测与聚类结果
- `*_angle.mat`
  - 测角结果
- `*_rd.gif`
  - RD 动图
- `*_det.gif`
  - 检测动图
- `*_angle.gif`
  - 测角动图

## 运行方式

1. 打开 [`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m)
2. 在最前面的参数区修改数据目录、开关和算法参数
3. 直接运行脚本

如果 `cfg.paths.data_folders` 为空，程序会弹出目录选择框。

## 文档维护建议

当前建议把文档分工固定为：

- [`apps/run_batch_pipeline.m`](./apps/run_batch_pipeline.m)：第一参考
- [`README.md`](./README.md)：整体结构与项目概览
- [`docs/usage_guide.md`](./docs/usage_guide.md)：详细使用说明

以下内容发生变化时，应同步修改文档：

- 原始目录结构变化
- 入口参数区字段变化
- 主流程顺序变化
- 模块职责变化
- 输出结果规则变化
