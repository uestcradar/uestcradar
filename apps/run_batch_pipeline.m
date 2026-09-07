%RUN_BATCH_PIPELINE Matlab_Helium 批处理总入口脚本。
%
% 本脚本采用“从上到下”的一页式流程组织方式：
% 1. 先在脚本最前面集中设置全部路径、开关和算法参数
% 2. 再按顺序执行“原始数据定位 -> 解析 -> 预处理 -> RD -> 检测 -> 聚类 -> 测角 -> 保存 -> 绘图”
% 3. 所有子模块都只负责单一环节，主流程顺序统一在本脚本中清晰展示


cfg = struct();

%% 1. 参数区：输入与输出路径
cfg.paths.data_folders = { ...
% 'F:\0610数据\LFM_20M_64_1024' ...
}; % 待批处理的数据集目录列表；每个目录应符合“TX/发射配置子目录 + RX/多批次子目录”的原始结构。
cfg.paths.tx_root_dir = 'TX'; % 发射参考信号根目录名称；其下通常还会再分一层具体发射配置子目录。
cfg.paths.tx_subdir_pattern = '*'; % 发射配置子目录匹配规则；默认读取 TX 下第一个满足条件的子目录。
cfg.paths.tx_file_name = 'lfm_tx.bin'; % 发射参考波形文件名称；位于 TX 的具体配置子目录中。
cfg.paths.tx_meta_name = 'metadata.json'; % 发射参考元数据文件名称；位于 TX 的具体配置子目录中。
cfg.paths.rx_root_dir = 'RX'; % 接收数据根目录名称；其下通常按采集批次继续分子目录。
cfg.paths.rx_meta_name = 'metadata.json'; % 接收批次元数据文件名称。
cfg.paths.rx_pattern = '*_data_seg*.mat'; % RX 前端预处理 mat 文件匹配规则。
cfg.paths.frontend_mat_file = ''; % 前端 .mat 文件路径；空=弹窗选择。
cfg.paths.result_dir_name = 'Results'; % 结果输出目录名称；最终会在每个数据集目录下生成该子目录。

%% 1b. 参数区：波位排布（从帧内嵌元数据自动提取）
cfg.beam.output_rd_per_beam = true;     % 是否保留逐波位 RD_Proc_beam*.mat；调试用，可设为 false 节省磁盘
cfg.beam.test_single_beam = 0;          % 单波位测试模式：0=全部波位；N=仅处理波位 N
cfg.beam.max_azimuth = 10;              % 方位角上限 (°)；az>此值跳过；inf=不限制
cfg.beam.min_azimuth = -10;             % 方位角下限 (°)；az<此值跳过；-inf=不限制
cfg.beam.max_elevation = 20;           % 俯仰角上限 (°)；el>此值跳过；inf=不限制
cfg.beam.min_elevation = 5;             % 俯仰角下限 (°)；el<此值跳过（负俯仰打地）；-inf=不限制

%% 2. 参数区：运行开关
cfg.run.do_process = true; % 是否重新执行 RD 处理；false 表示直接复用已有 RD_Proc_*.mat。
cfg.run.do_detect = true; % 是否执行 CFAR 检测；通常保持 true，除非只想验证前级数据。
cfg.run.do_cluster = true; % 是否执行 DBSCAN 聚类；关闭后只保留检测点，不输出聚类编号。
cfg.run.do_angle = true; % 是否执行单脉冲测角；要求 RD 结果中存在方位差与俯仰差通道。
cfg.run.do_plot = true; % 是否生成 Timeline GIF（RD 图）
cfg.run.do_plot_3d = true; % 是否生成 3D 航迹 GIF + 静态图

%% 3. 参数区：预处理参数
cfg.preprocess.do_dw_calibrate = true; % 是否自动标定直达波距离零点；true 表示利用首个 CPI 自动估计对齐位置。
cfg.preprocess.dw_bin_manual = 1; % 手动指定的直达波 bin；仅在 do_dw_calibrate=false 时生效。
cfg.preprocess.do_dw_blank = true; % 调试：关掉空白，确认目标是否被误清零
cfg.preprocess.do_subsample_align = true; % 是否执行子采样级别的精细对齐；用于修正非整数采样偏移。
cfg.preprocess.do_freq_comp = false; % 是否执行频偏补偿；调试阶段关闭，避免把目标多普勒误当频偏补偿
cfg.preprocess.do_fast_dc_remove = true; % 是否在快时间维先做逐脉冲去均值；用于抑制直流和静态偏置。
cfg.preprocess.n_guard = 10; % 直达波空白保护单元数；在发射脉宽末端额外扩展该范围一并置零。

%% 4. 参数区：雷达常量与元数据读取说明
cfg.radar.c = 3e8; % 光速常量，单位米每秒；用于距离分辨率和波长计算。
cfg.radar.fc = 9.5e9; % 雷达载频，单位 Hz；用于波长和速度轴计算。
% 说明：采样率、PRI 点数、带宽等发射参数不是在这里手动填写，而是在解析阶段从 TX 的具体配置子目录中的 metadata.json 自动读取。
% 其中当前代码直接使用的自动读取参数主要有：sample_rate、PRI；若 metadata.json 中包含 waveform 信息，也会一并写入 parse_info_*.mat。

%% 5. 参数区：RD 处理参数
cfg.rd.n_cpi = 256; % CPI 脉冲数；多波位模式下由波位文件逐波位覆写。
cfg.rd.n_overlap = 0; % 块间重叠脉冲数；多波位模式下固定为 0（无重叠）。
cfg.rd.max_range_m = 700; % 最大处理距离，单位米；只保留该距离以内的距离单元参与后续处理。
cfg.rd.frames_per_chunk = 4096; % 预处理分块大小，用于 freq_offsets 数组长度估算
cfg.rd.do_mti_twopulse = true; % 是否执行两脉冲相消；用于进一步增强运动目标、压制静态背景。
cfg.rd.zero_doppler_cells = 1;   % 零多普勒清除半宽度；RD 后 DC ± N 格置零；0=仅清DC本身；负值=不清除

%% 6. 参数区：目标检测参数
cfg.detect.range_window_m = [200, 700]; % 检测阶段使用的距离显示/分析范围，单位米。
cfg.detect.velocity_window_mps = [-50, 50]; % 检测阶段使用的速度显示/分析范围，单位米每秒。
cfg.detect.cfar_guard_r = 4; % CFAR 在距离维的保护单元数；避免参考窗污染目标主瓣。
cfg.detect.cfar_guard_d = 8; % CFAR 在速度维的保护单元数；避免参考窗污染目标主瓣。
cfg.detect.cfar_ref_r = 16; % CFAR 在距离维的参考单元数；用于估计局部噪声背景。
cfg.detect.cfar_ref_d = 32; % CFAR 在速度维的参考单元数；用于估计局部噪声背景。
cfg.detect.cfar_pfa = 1e-9; % CFAR 虚警概率；设置为 1e-9，目的是进一步压低虚警点数量，让检测结果更保守。
cfg.detect.frame_step = 1; % 检测/聚类/测角抽帧步长；默认1（全帧），独立于 cfg.plot.frame_step。

%前:  guard_r=2, guard_d=4, ref_r=8, ref_d=16, pfa=1e-6
%后: guard_r=4, guard_d=8, ref_r=16, ref_d=32, pfa=1e-9

%% 7. 参数区：聚类参数
cfg.cluster.dbscan_eps = 2; % DBSCAN 邻域半径；单位是“距离 bin / 速度 bin”的索引尺度。
cfg.cluster.dbscan_min = 4; % DBSCAN 成簇最少点数；低于该点数的检测点会被视为噪声或孤立点。

%% 7b. 参数区：融合参数
cfg.fusion.dbscan_eps_grid = 2;      % 融合 Grid-DBSCAN 邻域半径（网格单元）；≥2 可有效过滤孤立鬼影
cfg.fusion.dbscan_minpts_grid = 2;   % 融合 Grid-DBSCAN 最少点数；1=不过滤，2=最少两个点才成簇

%% 8. 参数区：测角参数
cfg.angle.range_window_m = [200, 700]; % 测角阶段保留目标的距离范围，单位米。
cfg.angle.velocity_window_mps = [-50, 50]; % 测角阶段保留目标的速度范围，单位米每秒。
cfg.angle.min_display_power_dB = 120; % 测角时参与输出的最小显示功率阈值，单位 dB。

% --- LUT 查表测角 ---
cfg.angle.use_lut = true;         % 是否启用 LUT 查表 + 2D 解耦测角
cfg.angle.k_az = 4.0;            % 方位单脉冲斜率系数（无量纲，需根据天线参数估算）
cfg.angle.k_el = 4.0;            % 俯仰单脉冲斜率系数
cfg.angle.lut_roi_deg = 5.0;      % LUT 角度覆盖范围 ±ROI（度），应 ≥ 波位间隔的一半
cfg.angle.lut_step_deg = 0.1;     % LUT 栅格步长（度）

%% 9. 参数区：结果导出参数
cfg.export.save_analysis_mat = true; % 是否保存检测结果和测角结果 mat 文件；便于后续直接复用分析结果。
cfg.export.gif_delay = 0.1; % GIF 帧间延时（最小 0.01s，GIF 格式限制）
cfg.export.keep_rd_mat = true; % 是否保留 RD_Proc_*.mat；若只关心最终图像且不复用 RD，可改为 false。

%% 10. 参数区：跟踪参数（EKF + GNN 多目标跟踪）
cfg.track.enable = true;              % 是否启用多目标跟踪
cfg.track.radar_height = 30;          % 雷达架高 (m)
cfg.track.range_noise_std = 20;       % 距离量测噪声标准差 (m)
cfg.track.angle_noise_std_deg = 3;    % 角度量测噪声标准差 (°)
cfg.track.vr_noise_std = 2.0;         % 径向速度量测噪声标准差 (m/s)
cfg.track.decimation = 1;             % 跟踪降采样：每 N 帧取 1 帧
cfg.track.q = 0.1;                    % 过程噪声强度因子
cfg.track.v_tan_std_init = 10.0;      % 初始切向速度不确定性 (m/s)
cfg.track.M = 7;                      % M/N 逻辑：最少命中次数（7/9 确认，滤除间歇性杂波）
cfg.track.N = 9;                      % M/N 逻辑：判定窗口帧数
cfg.track.max_predictions = 3;        % 连续丢失终止阈值：航迹连续未关联帧数超过该值即终止
cfg.track.gate_confidence = 0.99;    % Chi-squared 关联门限置信度
cfg.track.max_history_length = 100;   % 航迹历史最大存储点数

%% 11. 参数区：绘图参数
cfg.plot.range_window_m = [200, 700]; % 绘图显示的距离范围，单位米。
cfg.plot.velocity_window_mps = [-50, 50]; % 绘图显示的速度范围，单位米每秒。
cfg.plot.clim_dB = [110, 155]; % RD 幅度图颜色条范围，单位 dB；用于统一不同帧的显示亮度。
cfg.plot.frame_step = 1; % GIF 抽帧步长；1=全帧，20=每20帧取1帧
cfg.plot.raw_rd_gif = true;   % 是否逐波位生成原始 RD 热力图 GIF（不经 CFAR）
cfg.plot.do_point_trace_2d = true; % 是否生成二维点迹图（笛卡尔地面投影，全时刻聚合，虚线分隔波位）
cfg.plot.do_track_map_2d = true; % 是否生成二维航迹图（笛卡尔地面投影，静态点线图，每条航迹逐点标记）
cfg.plot.save_plot_data = true; % 是否保存三类图所需数据（PlotData_*.mat），供 replay_plots.m 复现

%% 12. 参数区：运行时输出
cfg.runtime.status_cb = @(msg) fprintf('%s\n', msg); % 统一日志输出回调；所有模块都通过它打印中文状态。

%% 13. 初始化运行环境
this_dir = fileparts(mfilename('fullpath'));
project_root = fileparts(this_dir);
addpath(project_root);
addpath(genpath(fullfile(project_root, 'src')));

if isempty(cfg.paths.data_folders)
    selected = uigetdir(project_root, '请选择需要处理的数据集目录');
    if isequal(selected, 0)
        fprintf('[入口] 已取消运行。\n');
        return;
    end
    cfg.paths.data_folders = {selected};
end

fprintf('\n========== Matlab_Helium 批处理开始：共 %d 个数据目录 ==========\n', numel(cfg.paths.data_folders));
t_total = tic;
run_ts_global = char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'));

%% 14. 逐个数据集执行完整流程
for di = 1:numel(cfg.paths.data_folders)
    data_dir = cfg.paths.data_folders{di};
    [~, dataset_name] = fileparts(data_dir);
    result_root_dir = fullfile(data_dir, cfg.paths.result_dir_name);
    if ~exist(result_root_dir, 'dir')
        mkdir(result_root_dir);
    end
    result_dir = fullfile(result_root_dir, run_ts_global);
    if ~exist(result_dir, 'dir')
        mkdir(result_dir);
    end

    fprintf('\n[%d/%d] 当前数据集：%s\n', di, numel(cfg.paths.data_folders), dataset_name);
    fprintf('  [输出] 本次结果目录：%s\n', result_dir);

    %% 14.1 定位输入
    fprintf('  [步骤1] 定位输入文件\n');
    lg = cfg.runtime.status_cb;

    % TX 目录
    tx_root = fullfile(data_dir, cfg.paths.tx_root_dir);
    tx_entries = dir(fullfile(tx_root, cfg.paths.tx_subdir_pattern));
    tx_entries = tx_entries([tx_entries.isdir] & ~ismember({tx_entries.name}, {'.', '..'}));
    if isempty(tx_entries)
        error('run_batch_pipeline:MissingTxDir', '未找到 TX 配置目录: %s', tx_root);
    end
    tx_dir = fullfile(tx_entries(1).folder, tx_entries(1).name);

    % RX：前端预处理 .mat 文件
    if ~isempty(cfg.paths.frontend_mat_file)
        mat_file = cfg.paths.frontend_mat_file;
    else
        [fn, fp] = uigetfile(fullfile(data_dir, cfg.paths.rx_root_dir, cfg.paths.rx_pattern), ...
            '选择前端预处理 .mat 文件');
        if isequal(fn, 0), fprintf('[入口] 已取消。\n'); return; end
        mat_file = fullfile(fp, fn);
    end
    [~, batch_name] = fileparts(mat_file);

    batch_result_dir = fullfile(result_dir, batch_name);
    if ~exist(batch_result_dir, 'dir'), mkdir(batch_result_dir); end
    fprintf('  [批次] %s\n', batch_name);

    %% 14.2 加载前端数据
    fprintf('  [步骤2] 加载前端预处理数据\n');
    parse_bundle = load_frontend_mat(mat_file, tx_dir, lg);

    % ---- 波位排布加载（从帧内嵌波位元数据提取）----
    if ~isfield(parse_bundle, 'beam_meta') || isempty(parse_bundle.beam_meta)
        error('run_batch_pipeline:MissingBeamMeta', ...
            'parse_bundle 缺少 beam_meta。请使用新解析器重新解析数据 (cfg.run.do_parse=true)。');
    end
    beam_schedule = build_beam_schedule_from_meta(parse_bundle.beam_meta, ...
        parse_bundle.rx_param.pri_per_frame);

    % 报告剔除的异常扫描
    if ~isempty(beam_schedule.invalid_scans)
        lg(sprintf('[波位] 已剔除 %d 个异常/不完整扫描: %s', ...
            numel(beam_schedule.invalid_scans), mat2str(beam_schedule.invalid_scans)));
    end

    % ---- 生成单脉冲 LUT（若启用 LUT 测角）----
    if cfg.angle.use_lut
        monopulse_lut = mono_angle('generate_lut', ...
            cfg.angle.k_az, cfg.angle.k_el, cfg.angle.lut_roi_deg, cfg.angle.lut_step_deg, ...
            beam_schedule);
    else
        monopulse_lut = [];
    end

    % 构建基础 RD 上下文（pri_len, fs, prt 等基础参数）
    rd_ctx = build_rd_context(parse_bundle.rx_param, parse_bundle.tx, cfg.rd, cfg.radar);
    % 根据波位排布确定 chunk 数（= 总帧数）
    rd_ctx.num_chunks = max(floor(double(parse_bundle.rx_param.total_pri) / beam_schedule.total_pulses), rd_ctx.num_chunks);

    % ============ 多波位 TWS 模式 ============
    lg(sprintf('\n---------- 多波位 TWS 模式：%d 个波位 ----------', beam_schedule.num_beams));

    % 预处理只 init 一次（直达波对齐对所有波位通用）
    fprintf('  [步骤4] 初始化预处理模块（共享）\n');
    raw_spec = struct();
    raw_spec.data_dir = data_dir;
    raw_spec.parsed_ch1_file = parse_bundle.rx_channel_files{1};
    raw_spec.parsed_ch1_var  = parse_bundle.channel_var_names{1};
    raw_spec.initial_scan_pri = beam_schedule.initial_scan_pri;
    [~, shared_preproc] = preprocess('init', raw_spec, parse_bundle.tx, rd_ctx, cfg.preprocess, lg);
    lg(sprintf('  [预处理] 直达波 bin=%d，对齐模式=%s', shared_preproc.dw_bin, shared_preproc.dw_mode));

    process_cfg = struct();
    process_cfg.process = cfg.rd;
    process_cfg.preprocess = cfg.preprocess;

    % all_raw_plots 列定义：[r(m), az(deg), el(deg), vr(m/s), time(s), pwr(dB), beam_id, scan_id]
    all_raw_plots = [];
    scan_period = beam_schedule.total_pulses / parse_bundle.rx_param.prf;
    num_cpi_files = numel(parse_bundle.rx_param.cpi_files);

    % 基础分辨率（用于融合网格量化，所有波位共用）
    base_r_res = rd_ctx.c / (2 * rd_ctx.fs);           % 距离分辨率 (m)
    base_v_res = rd_ctx.lambda / (2 * rd_ctx.prt * beam_schedule.pulses_per_dwell(1));  % 速度分辨率 (m/s)

    for beam_id = 1:beam_schedule.num_beams
        % 单波位测试模式：跳过非目标波位
        if cfg.beam.test_single_beam > 0 && beam_id ~= cfg.beam.test_single_beam
            continue;
        end
        beam_az = beam_schedule.beam_positions(beam_id, 1);
        beam_el = beam_schedule.beam_positions(beam_id, 2);
        % 跳过角度超限的波位（方位/俯仰上下限）
        if beam_az > cfg.beam.max_azimuth || beam_az < cfg.beam.min_azimuth ...
           || beam_el > cfg.beam.max_elevation || beam_el < cfg.beam.min_elevation
            continue;
        end
        pulses_per_dwell = beam_schedule.pulses_per_dwell(beam_id);

        % 构建波位专用 rd_ctx（逐波位覆写 CPI 脉冲数与重叠）
        beam_rd_cfg = cfg.rd;
        beam_rd_cfg.n_cpi = pulses_per_dwell;
        beam_rd_cfg.n_overlap = 0;
        beam_rx_param = parse_bundle.rx_param;
        beam_rd_ctx = build_rd_context(beam_rx_param, parse_bundle.tx, beam_rd_cfg, cfg.radar);

        % 每波位每扫描一个 CPI
        beam_rd_ctx.pulses_per_scan = beam_schedule.total_pulses;
        beam_rd_ctx.total_blocks = beam_schedule.total_scans;
        beam_rd_ctx.num_chunks = beam_rd_ctx.total_blocks;
        beam_rd_ctx.beam_start_offset = sum(beam_schedule.pulses_per_dwell(1:beam_id-1));
        beam_rd_ctx.initial_scan_pri = beam_schedule.initial_scan_pri;

        if mod(beam_id, 20) == 1 || beam_id == beam_schedule.num_beams
            lg(sprintf('[波位 %3d/%d] az=%+.1f°, el=%+.1f°, 帧数=%d', ...
                beam_id, beam_schedule.num_beams, beam_az, beam_el, beam_rd_ctx.total_blocks));
        end

        % ---- RD 处理 ----
        if cfg.run.do_process
            out_file = process_rd_beam(beam_id, beam_az, beam_el, data_dir, parse_bundle, ...
                beam_rd_ctx, shared_preproc, process_cfg, batch_result_dir, lg);
        else
            % 在所有历史运行目录中搜索波位 RD 文件
            rd_pattern = sprintf('RD_Proc_beam%03d_*.mat', beam_id);
            out_file = '';
            all_run_dirs = get_all_run_output_dirs(data_dir, cfg.paths.result_dir_name);
            for ri = 1:numel(all_run_dirs)
                beam_dir = fullfile(all_run_dirs{ri}, batch_name, sprintf('beam_%03d', beam_id));
                if exist(beam_dir, 'dir')
                    entries = dir(fullfile(beam_dir, rd_pattern));
                    if ~isempty(entries)
                        [~, idx] = max([entries.datenum]);
                        out_file = fullfile(entries(idx).folder, entries(idx).name);
                        break;
                    end
                end
            end
            if isempty(out_file)
                error('run_batch_pipeline:MissingBeamRD', ...
                    '未找到波位 %d 的 RD 文件（模式=%s）', beam_id, rd_pattern);
            end
        end

        % ---- 检测 + 聚类 + 测角 ----
        if cfg.run.do_detect
            rd = matfile(out_file);
            r_axis = builtin('double', rd.r_axis_full);
            v_axis = builtin('double', rd.v_axis_full);

            detect_r_range = cfg.detect.range_window_m;
            detect_v_range = cfg.detect.velocity_window_mps;
            r_mask = r_axis >= detect_r_range(1) & r_axis <= detect_r_range(2);
            v_mask = v_axis >= detect_v_range(1) & v_axis <= detect_v_range(2);
            r_gate = r_axis(2) - r_axis(1);
            v_gate = v_axis(2) - v_axis(1);
            % r_base/v_base：显示窗口在全轴上的起始下标偏移；后续检测点下标需减去它，映射到子窗口坐标系
            r_base = find(r_mask, 1, 'first') - 1;
            v_base = find(v_mask, 1, 'first') - 1;
            r_disp = r_axis(r_mask);
            v_disp = v_axis(v_mask);

            cfar_p = struct('guard_r', cfg.detect.cfar_guard_r, 'guard_d', cfg.detect.cfar_guard_d, ...
                'ref_r', cfg.detect.cfar_ref_r, 'ref_d', cfg.detect.cfar_ref_d, 'pfa', cfg.detect.cfar_pfa);
            has_az = ismember('RD_Az_All', who(rd));
            has_el = ismember('RD_El_All', who(rd));
            has_angle = cfg.run.do_angle && has_az && has_el;

            n_blocks = builtin('double', rd.total_blocks);
            frame_ids = 1:cfg.detect.frame_step:n_blocks;

            for fi = 1:numel(frame_ids)
                k = frame_ids(fi);
                rd0 = rd.RD_Sum_All(:, :, k);   % 和通道
                rd_sub = rd0(r_mask, v_mask);
                pwr = abs(rd_sub).^2;

                det_mask_full = cfar_2d(abs(rd0).^2, cfar_p);
                det_mask = det_mask_full & r_mask(:) & v_mask(:)';
                [det_r_idx, det_v_idx] = find(det_mask);
                det_r_idx = det_r_idx - r_base; det_v_idx = det_v_idx - v_base;
                n_det = numel(det_r_idx);

                if cfg.run.do_cluster && n_det >= cfg.cluster.dbscan_min
                    pts_phys = [r_axis(det_r_idx + r_base)' ./ r_gate, ...
                                v_axis(det_v_idx + v_base)' ./ v_gate];
                    [clu_ids, n_clu] = dbscan_cluster(pts_phys, cfg.cluster.dbscan_eps, cfg.cluster.dbscan_min);
                else
                    clu_ids = zeros(n_det, 1, 'int32');
                    n_clu = 0;
                end

                if has_angle && n_clu > 0
                    rd1 = rd.RD_Az_All(:, :, k);   % 方位差通道
                    rd2 = rd.RD_El_All(:, :, k);   % 俯仰差通道
                    rd1_sub = rd1(r_mask, v_mask);
                    rd2_sub = rd2(r_mask, v_mask);
                    az_ratio = real(rd1_sub ./ (rd_sub + eps));
                    el_ratio = real(rd2_sub ./ (rd_sub + eps));

                    if cfg.angle.use_lut && ~isempty(monopulse_lut)
                        [r_m, v_m, az_off, el_off, ~] = mono_angle( ...
                            r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
                            pwr, az_ratio, el_ratio, [], ...
                            cfg.angle.range_window_m, cfg.angle.velocity_window_mps, cfg.angle.min_display_power_dB, ...
                            monopulse_lut, beam_id);
                    else
                        r_m = []; v_m = []; az_off = []; el_off = [];
                    end

                    if ~isempty(r_m)
                        n_pts = numel(r_m);
                        % 从 mono_angle 返回的 r_m/v_m 直接反查功率，避免 cluster_id 映射错位
                        pwr_dB = zeros(n_pts, 1);
                        for p = 1:n_pts
                            [~, ri] = min(abs(r_disp - r_m(p)));
                            [~, vi] = min(abs(v_disp - v_m(p)));
                            pwr_dB(p) = 10 * log10(pwr(ri, vi) + eps);
                        end

                        % 角度：波位中心角 + LUT 偏移量
                        plot_az = beam_az + az_off(:);
                        plot_el = beam_el + el_off(:);

                        % 测量时刻：扫描起点 + 波位驻留中心偏移
                        dwell_center_offset = sum(beam_schedule.pulses_per_dwell(1:beam_id-1)) + pulses_per_dwell / 2;
                        time_val = ((k - 1) * beam_schedule.total_pulses + dwell_center_offset) / parse_bundle.rx_param.prf;
                        all_raw_plots = [all_raw_plots; ...
                            r_m(:), plot_az, plot_el, ...
                            v_m(:), repmat(time_val, n_pts, 1), ...
                            pwr_dB(:), repmat(beam_id, n_pts, 1), ...
                            repmat(k, n_pts, 1)]; %#ok<AGROW>
                    end
                end
            end
        end

        % 逐波位原始 RD GIF（不经 CFAR，调试用）
        if cfg.plot.raw_rd_gif && exist(out_file, 'file')
            plot_figures('raw_rd_gif', out_file, beam_id, beam_az, beam_el, cfg);
        end

        % 可选：清理逐波位 RD 文件以节省磁盘
        if ~cfg.beam.output_rd_per_beam && cfg.run.do_process && exist(out_file, 'file')
            delete(out_file);
        end

        % 逐波位清理：119 波位循环中防止 matfile 句柄和中间变量堆积
        clear rd rd0 rd1 rd2 rd_sub;
    end

    % ---- 异常扫描已由 RD 层跳过，检测结果已是连续正常扫描 ----

    if isempty(all_raw_plots)
        lg('[融合] 无任何检测目标（all_raw_plots 为空），跳过融合、跟踪与绘图。');
        continue;
    end

    % ============ 跨波位融合（逐扫描进行）============
    fprintf('\n  [步骤9] 多波位点迹融合\n');
    fusion_params = struct();
    fusion_params.resolutions = [base_r_res, 5.0, 5.0, base_v_res];
    fusion_params.dbscan_eps_grid = cfg.fusion.dbscan_eps_grid;
    fusion_params.dbscan_minpts_grid = cfg.fusion.dbscan_minpts_grid;

    scan_ids = unique(all_raw_plots(:, 8));
    fused_plots = [];
    total_input = 0; total_ghosts = 0; total_after = 0; total_final = 0;

    for si = 1:numel(scan_ids)
        sid = scan_ids(si);
        raw_scan = all_raw_plots(all_raw_plots(:, 8) == sid, 1:7);  % 去掉 scan_id 列
        [fused_scan, stats_scan] = fuse_beam_plots(raw_scan, fusion_params);
        if ~isempty(fused_scan)
            % fused_plots 列: [r(m), az(deg), el(deg), vr(m/s), time(s), scan_id]
            fused_plots = [fused_plots; fused_scan, repmat(sid, size(fused_scan, 1), 1)]; %#ok<AGROW>
        end
        total_input  = total_input  + stats_scan.num_input;
        total_ghosts = total_ghosts + stats_scan.num_ghosts_suppressed;
        total_after  = total_after  + stats_scan.num_after_fusion;
        total_final  = total_final  + stats_scan.num_final;
    end

    % 保存融合结果
    ts_out = char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'));
    fused_mat = fullfile(batch_result_dir, sprintf('Fused_Targets_%s.mat', ts_out));
    save(fused_mat, 'fused_plots', 'all_raw_plots', 'beam_schedule', '-v7.3');
    lg(sprintf('  [保存] 融合结果已写入：%s', fused_mat));
    lg(sprintf('  [融合统计] 输入=%d, 鬼影剔除=%d, 融合后=%d, 最终=%d', ...
        total_input, total_ghosts, total_after, total_final));

    % ---- 多目标跟踪（EKF + GNN）----
    total_scan_frames = beam_schedule.total_scans;  % 用实际扫描数，包含无目标帧

    if cfg.track.enable
        fprintf('  [步骤10] 多目标跟踪 (EKF+GNN)\n');

        tracker_params = struct();
        tracker_params.dt_frame = scan_period * cfg.track.decimation;
        fprintf('  [跟踪] dt_frame = %.3f s (scan_period=%.3f × decimation=%d)\n', ...
            tracker_params.dt_frame, scan_period, cfg.track.decimation);
        tracker_params.radar_height = cfg.track.radar_height;
        sigma_r  = cfg.track.range_noise_std;
        sigma_a  = deg2rad(cfg.track.angle_noise_std_deg);
        sigma_e  = deg2rad(cfg.track.angle_noise_std_deg);
        sigma_vr = cfg.track.vr_noise_std;
        tracker_params.measurement_noise = diag([sigma_r^2, sigma_a^2, sigma_e^2, sigma_vr^2]);
        Q_base = [tracker_params.dt_frame^3/3, tracker_params.dt_frame^2/2;
                  tracker_params.dt_frame^2/2, tracker_params.dt_frame];
        tracker_params.process_noise_matrix = blkdiag(Q_base, Q_base, Q_base) * cfg.track.q;
        tracker_params.M = cfg.track.M;
        tracker_params.N = cfg.track.N;
        tracker_params.max_predictions = cfg.track.max_predictions;
        tracker_params.gate_confidence = cfg.track.gate_confidence;
        tracker_params.max_history_length = cfg.track.max_history_length;
        tracker_params.v_tan_std_init = cfg.track.v_tan_std_init;

        [tracks, track_id_counter] = track_init();
        decimated_frames = 1:cfg.track.decimation:total_scan_frames;
        track_results = cell(total_scan_frames, 1);
        for dd = 1:numel(decimated_frames)
            fi = decimated_frames(dd);
            if ~isempty(fused_plots)
                meas_idx = fused_plots(:, 6) == fi;   % 第 6 列 = scan_id
                raw_meas = fused_plots(meas_idx, 1:5); % 取 [r, az, el, vr, time] 五列
            else
                raw_meas = [];
            end
            if ~isempty(raw_meas)
                raw_meas(:, 2:3) = deg2rad(raw_meas(:, 2:3));  % 角度转弧度
                raw_meas(:, 4) = -raw_meas(:, 4);  % 翻转vr符号: 雷达(正=接近)→EKF(正=远离)
            end
            [tracks, track_id_counter] = tracker_3D_EKF(tracks, raw_meas, track_id_counter, tracker_params);
            track_results{fi} = tracks;
        end

        n_active = sum(~[tracks.is_terminated]);
        n_confirmed = sum([tracks.is_confirmed]);
        lg(sprintf('  [跟踪] 总航迹=%d, 活跃=%d, 已确认=%d', numel(tracks), n_active, n_confirmed));

        % 保存跟踪最终状态
        fprintf('  [跟踪] 保存结果...\n');
        track_mat = fullfile(batch_result_dir, sprintf('Tracks_%s.mat', ts_out));
        save(track_mat, 'tracks', '-v7.3');

        if cfg.plot.do_track_map_2d
            fprintf('  [步骤10b] 二维航迹图\n');
            plot_figures('track_map_2d', tracks, beam_schedule, batch_result_dir);
        end

        % 保存三类图所需数据（供 replay_plots.m 复现完全一致的图）
        if cfg.plot.save_plot_data
            plot_data_file = fullfile(batch_result_dir, sprintf('PlotData_%s.mat', ts_out));
            radar_height = cfg.track.radar_height;
            frame_step = cfg.plot.frame_step;
            gif_delay = cfg.export.gif_delay;
            save(plot_data_file, 'fused_plots', 'beam_schedule', 'tracks', ...
                'track_results', 'total_scan_frames', 'radar_height', ...
                'frame_step', 'gif_delay', '-v7.3');
            fprintf('  [保存] 绘图数据已写入：%s\n', plot_data_file);
        end
    else
        track_results = {};
    end

    % ---- 绘图（如果启用）----
    if cfg.run.do_plot
        fprintf('  [步骤11] 逐帧动图 (RD)\n');
        plot_figures('beam_timeline_gif', all_raw_plots, fused_plots, total_scan_frames, batch_result_dir, cfg, track_results);
    end
    if cfg.run.do_plot_3d && cfg.track.enable
        fprintf('  [步骤12] 3D 航迹动图\n');
        plot_figures('tracking_3d_gif', fused_plots, total_scan_frames, track_results, ...
            cfg.track.radar_height, batch_result_dir, cfg.plot.frame_step, cfg.export.gif_delay);
    end
    if cfg.plot.do_point_trace_2d
        fprintf('  [步骤13] 二维点迹图\n');
        plot_figures('point_trace_2d', fused_plots, beam_schedule, batch_result_dir);
    end

    % 可选：清理逐波位目录
    if ~cfg.beam.output_rd_per_beam && cfg.run.do_process
        for beam_id = 1:beam_schedule.num_beams
            bdir = fullfile(batch_result_dir, sprintf('beam_%03d', beam_id));
            if exist(bdir, 'dir')
                rmdir(bdir, 's');
            end
        end
    end

end

fprintf('\n========== 全部处理完成，总耗时 %.1f 分钟 ==========\n', toc(t_total) / 60);

%% 15. 本地辅助函数
function rd_ctx = build_rd_context(rx_param, tx, rd_cfg, radar_cfg)
    %BUILD_RD_CONTEXT 计算 RD 处理上下文。
    %
    % 输入：
    %   rx_param - 解析阶段输出的接收参数结构体
    %   tx       - 解析阶段输出的发射参考波形结构体
    %   rd_cfg   - RD 处理参数结构体
    %   radar_cfg - 雷达常量配置结构体
    % 输出：
    %   rd_ctx   - RD 处理共用上下文
    % 作用：
    %   统一计算采样率、PRI、距离轴、多普勒轴、块划分参数和匹配滤波参考。

    rd_ctx = struct();
    rd_ctx.fs = builtin('double', rx_param.sample_rate);
    rd_ctx.pri_len = builtin('double', rx_param.pri_len);
    rd_ctx.total_frames_global = builtin('double', rx_param.total_pri);
    rd_ctx.prt = rd_ctx.pri_len / rd_ctx.fs;
    rd_ctx.lambda = radar_cfg.c / radar_cfg.fc;
    rd_ctx.c = radar_cfg.c;
    rd_ctx.n_cpi = builtin('double', rd_cfg.n_cpi);
    rd_ctx.n_overlap = 0;  % TWS 模式无重叠
    rd_ctx.n_step = rd_ctx.n_cpi;
    r_res = radar_cfg.c / (2 * rd_ctx.fs);
    rd_ctx.max_calc_samples = min(round(rd_cfg.max_range_m / r_res), rd_ctx.pri_len);
    rd_ctx.r_axis_full = (0:rd_ctx.max_calc_samples - 1) * r_res;
    v_res = (rd_ctx.lambda / rd_ctx.prt) / (2 * rd_ctx.n_cpi);
    rd_ctx.v_axis_full = (-rd_ctx.n_cpi / 2 : rd_ctx.n_cpi / 2 - 1) * v_res;
    rd_ctx.v_res = v_res;
    rd_ctx.effective_frames = rd_ctx.total_frames_global - 1;
    rd_ctx.total_blocks = floor((rd_ctx.effective_frames - rd_ctx.n_cpi) / rd_ctx.n_step) + 1;
    rd_ctx.frames_per_chunk = builtin('double', rd_cfg.frames_per_chunk);
    rd_ctx.num_chunks = ceil(rd_ctx.effective_frames / rd_ctx.frames_per_chunk);
    rd_ctx.zero_doppler_cells = rd_cfg.zero_doppler_cells;
    rd_ctx.pw_samples = sum(abs(tx.data) > 0.01 * max(abs(tx.data)));
    ref_freq = fft(single(tx.data), rd_ctx.pri_len);
    rd_ctx.conj_ref_freq = conj(ref_freq) .* single(hamming(rd_ctx.pri_len));
end

function all_run_dirs = get_all_run_output_dirs(data_dir, result_dir_name)
%GET_ALL_RUN_OUTPUT_DIRS 获取全部历史运行的时间戳目录（从新到旧）。
    all_run_dirs = {};
    result_root_dir = fullfile(data_dir, result_dir_name);
    if ~exist(result_root_dir, 'dir'), return; end
    entries = dir(result_root_dir);
    entries = entries([entries.isdir]);
    names = {entries.name};
    mask = ~ismember(names, {'.', '..'});
    entries = entries(mask);
    ts_mask = ~cellfun('isempty', regexp({entries.name}, '^\d{8}_\d{6}$', 'once'));
    entries = entries(ts_mask);
    if isempty(entries), return; end
    [~, idx] = sort([entries.datenum], 'descend');
    entries = entries(idx);
    all_run_dirs = cell(1, numel(entries));
    for i = 1:numel(entries)
        all_run_dirs{i} = fullfile(entries(i).folder, entries(i).name);
    end
end
