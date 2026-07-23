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
cfg.paths.rx_batch_pattern = '*'; % 接收批次子目录匹配规则；默认读取 RX 下全部批次目录并按名称排序拼接。
cfg.paths.rx_meta_name = 'metadata.json'; % 接收批次元数据文件名称；默认使用第一批次的 metadata 作为接收参数来源。
cfg.paths.rx_pattern = 'cpi_*.bin'; % 单批次内的 CPI 文件匹配规则；会对所有批次中的 cpi_*.bin 统一排序收集。
cfg.paths.parse_info_pattern = 'parse_info_*.mat'; % 已解析索引文件匹配规则；关闭解析时用它加载最新一次解析结果。
cfg.paths.result_dir_name = 'Results'; % 结果输出目录名称；最终会在每个数据集目录下生成该子目录。

%% 1b. 参数区：波位排布（多波位 TWS 模式）
cfg.beam.beam_file = '波位格式_128同向.txt';     % 波位排布文件路径（相对于工程根目录，必需）
cfg.beam.output_rd_per_beam = true;     % 是否保留逐波位 RD_Proc_beam*.mat；调试用，可设为 false 节省磁盘

%% 2. 参数区：运行开关
cfg.run.do_parse = false; % 是否重新解析原始 bin 文件；true 表示重新生成 rx_ch*.mat 和 parse_info_*.mat。
cfg.run.do_process = false; % 是否重新执行 RD 处理；false 表示直接复用已有 RD_Proc_*.mat。
cfg.run.do_detect = true; % 是否执行 CFAR 检测；通常保持 true，除非只想验证前级数据。
cfg.run.do_cluster = true; % 是否执行 DBSCAN 聚类；关闭后只保留检测点，不输出聚类编号。
cfg.run.do_angle = true; % 是否执行单脉冲测角；要求 RD 结果中存在方位差与俯仰差通道。
cfg.run.do_plot = true; % 是否执行绘图与 GIF 导出；关闭后只保留 mat 结果。

%% 3. 参数区：预处理参数
cfg.preprocess.do_dw_calibrate = true; % 是否自动标定直达波距离零点；true 表示利用首个 CPI 自动估计对齐位置。
cfg.preprocess.dw_bin_manual = 854; % 手动指定的直达波 bin；仅在 do_dw_calibrate=false 时生效。
cfg.preprocess.do_dw_blank = true; % 是否对直达波附近样本置零；用于抑制直达波泄漏对后续 RD 的影响。
cfg.preprocess.do_subsample_align = true; % 是否执行子采样级别的精细对齐；用于修正非整数采样偏移。
cfg.preprocess.do_freq_comp = true; % 是否执行频偏补偿；用于减轻慢时间相位漂移。
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
cfg.rd.max_range_m = 2000; % 最大处理距离，单位米；只保留该距离以内的距离单元参与后续处理。
cfg.rd.frames_per_chunk = 4096; % 预处理分块大小，用于 freq_offsets 数组长度估算
cfg.rd.do_mti_twopulse = true; % 是否执行两脉冲相消；用于进一步增强运动目标、压制静态背景。

%% 6. 参数区：目标检测参数
cfg.detect.range_window_m = [300, 800]; % 检测阶段使用的距离显示/分析范围，单位米。
cfg.detect.velocity_window_mps = [-50, 50]; % 检测阶段使用的速度显示/分析范围，单位米每秒。
cfg.detect.cfar_guard_r = 2; % CFAR 在距离维的保护单元数；避免参考窗污染目标主瓣。
cfg.detect.cfar_guard_d = 4; % CFAR 在速度维的保护单元数；避免参考窗污染目标主瓣。
cfg.detect.cfar_ref_r = 8; % CFAR 在距离维的参考单元数；用于估计局部噪声背景。
cfg.detect.cfar_ref_d = 16; % CFAR 在速度维的参考单元数；用于估计局部噪声背景。
cfg.detect.cfar_pfa = 1e-6; % CFAR 虚警概率；这里设置为 10^-6，目的是进一步压低虚警点数量，让检测结果更保守。
cfg.detect.frame_step = 1; % 检测/聚类/测角抽帧步长；默认1（全帧），独立于 cfg.plot.frame_step。

%% 7. 参数区：聚类参数
cfg.cluster.dbscan_eps = 3; % DBSCAN 邻域半径；单位是“距离 bin / 速度 bin”的索引尺度。
cfg.cluster.dbscan_min = 3; % DBSCAN 成簇最少点数；低于该点数的检测点会被视为噪声或孤立点。

%% 8. 参数区：测角参数
cfg.angle.k_mono = 1.0; % 单脉冲比幅系数缩放因子；用于把差比值映射到角度刻度。
cfg.angle.range_window_m = [300, 800]; % 测角阶段保留目标的距离范围，单位米。
cfg.angle.velocity_window_mps = [-50, 50]; % 测角阶段保留目标的速度范围，单位米每秒。
cfg.angle.min_display_power_dB = 110; % 测角时参与输出的最小显示功率阈值，单位 dB。

% --- LUT 查表测角（替代线性 k_mono 公式）---
cfg.angle.use_lut = true;         % 是否启用 LUT 查表 + 2D 解耦测角；false 时回退到 k_mono 线性
cfg.angle.k_az = 25.0;            % 方位单脉冲斜率系数（无量纲，需根据天线参数估算）
cfg.angle.k_el = 25.0;            % 俯仰单脉冲斜率系数
cfg.angle.lut_roi_deg = 5.0;      % LUT 角度覆盖范围 ±ROI（度），应 ≥ 波位间隔的一半
cfg.angle.lut_step_deg = 0.1;     % LUT 栅格步长（度）

%% 9. 参数区：结果导出参数
cfg.export.save_analysis_mat = true; % 是否保存检测结果和测角结果 mat 文件；便于后续直接复用分析结果。
cfg.export.gif_delay = 0.05; % GIF 帧间延时，单位秒；值越小动画播放越快。
cfg.export.keep_parse_mat = true; % 是否保留解析阶段生成的 rx_ch*.mat 和 parse_info_*.mat；若每次都重新 parse，可改为 false。
cfg.export.keep_rd_mat = true; % 是否保留 RD_Proc_*.mat；若只关心最终图像且不复用 RD，可改为 false。

%% 10. 参数区：绘图参数
cfg.plot.range_window_m = [300, 800]; % 绘图显示的距离范围，单位米。
cfg.plot.velocity_window_mps = [-50, 50]; % 绘图显示的速度范围，单位米每秒。
cfg.plot.clim_dB = [110, 155]; % RD 幅度图颜色条范围，单位 dB；用于统一不同帧的显示亮度。
cfg.plot.frame_step = 20; % GIF 抽帧步长；例如 20 表示每隔 20 帧导出 1 帧动画。

%% 11. 参数区：运行时输出
cfg.runtime.status_cb = @(msg) fprintf('%s\n', msg); % 统一日志输出回调；所有模块都通过它打印中文状态。

%% 12. 初始化运行环境
this_dir = fileparts(mfilename('fullpath'));
project_root = fileparts(this_dir);
addpath(project_root);
addpath(genpath(fullfile(project_root, 'src')));

% 解析波位文件路径（必需）
if isempty(cfg.beam.beam_file)
    error('run_batch_pipeline:MissingBeamFile', '波位文件路径 cfg.beam.beam_file 未设置。多波位 TWS 模式需要波位排布文件。');
end
cfg.paths.beam_file = fullfile(project_root, cfg.beam.beam_file);
if ~exist(cfg.paths.beam_file, 'file')
    error('run_batch_pipeline:BeamFileNotFound', '波位文件不存在：%s', cfg.paths.beam_file);
end

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

%% 13. 逐个数据集执行完整流程
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

    %% 13.1 定位原始输入
    fprintf('  [步骤1] 定位原始输入文件\n');
    raw_specs = locate_raw_inputs(data_dir, cfg.paths);
    lg = cfg.runtime.status_cb;
    lg(sprintf('  [输入] 共发现 %d 个 RX 批次', numel(raw_specs)));

    % 多个批次时弹窗让用户选择处理哪些
    batch_names = {raw_specs.batch_name};
    if numel(raw_specs) > 1
        [sel_idx, ok] = listdlg('Name', '选择 RX 批次', ...
        'PromptString', '请选择要处理的 RX 批次：', ...
        'ListString', batch_names, ...
        'ListSize', [300 200], ...
        'InitialValue', 1:numel(raw_specs));
        if ok
            raw_specs = raw_specs(sel_idx);
        end
    end

    for bi = 1:numel(raw_specs)
        raw_spec = raw_specs(bi);
        batch_result_dir = fullfile(result_dir, raw_spec.batch_name);
        if ~exist(batch_result_dir, 'dir')
            mkdir(batch_result_dir);
        end
        raw_spec.output_dir = batch_result_dir;
        fprintf('  [批次 %d/%d] %s\n', bi, numel(raw_specs), raw_spec.batch_name);
        fprintf('    [输出] %s\n', batch_result_dir);

        %% 13.2 解析原始数据
        if cfg.run.do_parse
            fprintf('  [步骤2] 重新解析原始 bin 数据\n');
            parse_bundle = batch_parse_bin(raw_spec);
            else
            fprintf('  [步骤2] 读取已有解析结果\n');
            result_dir_name = cfg.paths.result_dir_name;
            parse_info_pattern = cfg.paths.parse_info_pattern;
            parse_bundle = load_latest_parse_bundle(data_dir, result_dir_name, parse_info_pattern, raw_spec.batch_name);
        end

        % ---- 波位排布加载 ----
        beam_schedule = parse_beam_schedule(cfg.paths.beam_file);

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
        lg = cfg.runtime.status_cb;

        % ============ 多波位 TWS 模式 ============
            lg(sprintf('\n---------- 多波位 TWS 模式：%d 个波位 ----------', beam_schedule.num_beams));

            % 预处理只 init 一次（直达波对齐对所有波位通用）
            fprintf('  [步骤4] 初始化预处理模块（共享）\n');
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
                beam_az = beam_schedule.beam_positions(beam_id, 1);
                beam_el = beam_schedule.beam_positions(beam_id, 2);
                pulses_per_dwell = beam_schedule.pulses_per_dwell(beam_id);

                % 构建波位专用 rd_ctx
                beam_rd_cfg = cfg.rd;
                beam_rd_cfg.n_cpi = pulses_per_dwell;
                beam_rd_cfg.n_overlap = 0;
                beam_rx_param = parse_bundle.rx_param;
                beam_rx_param.total_pri = pulses_per_dwell * num_cpi_files + 1;  % +1 补偿 effective_frames = total-1
                beam_rd_ctx = build_rd_context(beam_rx_param, parse_bundle.tx, beam_rd_cfg, cfg.radar);

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
                        beam_dir = fullfile(all_run_dirs{ri}, raw_spec.batch_name, sprintf('beam_%03d', beam_id));
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
                        rd0 = rd.RD_Sum_All(:, :, k);
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
                            rd1 = rd.RD_Az_All(:, :, k);
                            rd2 = rd.RD_El_All(:, :, k);
                            rd1_sub = rd1(r_mask, v_mask);
                            rd2_sub = rd2(r_mask, v_mask);
                            az_ratio = real(rd1_sub ./ (rd_sub + eps));
                            el_ratio = real(rd2_sub ./ (rd_sub + eps));

                            if cfg.angle.use_lut && ~isempty(monopulse_lut)
                                [r_m, v_m, az_off, el_off, ~] = mono_angle( ...
                                    r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
                                    pwr, az_ratio, el_ratio, cfg.angle.k_mono, ...
                                    cfg.angle.range_window_m, cfg.angle.velocity_window_mps, cfg.angle.min_display_power_dB, ...
                                    monopulse_lut, beam_id);
                                use_lut_angles = true;
                            else
                                [r_m, v_m, ~, ~, ~] = mono_angle( ...
                                    r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
                                    pwr, az_ratio, el_ratio, cfg.angle.k_mono, ...
                                    cfg.angle.range_window_m, cfg.angle.velocity_window_mps, cfg.angle.min_display_power_dB);
                                use_lut_angles = false;
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

                                % 角度：LUT 模式用 beam_center + offset，线性模式仅记录波束中心
                                if use_lut_angles
                                    plot_az = beam_az + az_off(:);
                                    plot_el = beam_el + el_off(:);
                                else
                                    plot_az = repmat(beam_az, n_pts, 1);
                                    plot_el = repmat(beam_el, n_pts, 1);
                                end

                                % 测量时刻：扫描起点 + 波位驻留中心偏移
                                dwell_center_offset = (beam_id - 1) * pulses_per_dwell + pulses_per_dwell / 2;
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

                % 可选：清理逐波位 RD 文件以节省磁盘
                if ~cfg.beam.output_rd_per_beam && cfg.run.do_process && exist(out_file, 'file')
                    delete(out_file);
                end
            end

            % ============ 跨波位融合（逐扫描进行）============
            fprintf('\n  [步骤9] 多波位点迹融合\n');
            fusion_params = struct();
            fusion_params.resolutions = [base_r_res, 5.0, 5.0, base_v_res];
            fusion_params.dbscan_eps_grid = 3;
            fusion_params.dbscan_minpts_grid = 1;

            scan_ids = unique(all_raw_plots(:, 8));
            fused_plots = [];
            total_input = 0; total_ghosts = 0; total_after = 0; total_final = 0;

            for si = 1:numel(scan_ids)
                sid = scan_ids(si);
                raw_scan = all_raw_plots(all_raw_plots(:, 8) == sid, 1:7);  % 去掉 scan_id 列
                [fused_scan, stats_scan] = fuse_beam_plots(raw_scan, fusion_params);
                if ~isempty(fused_scan)
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

            % ---- 绘图（如果启用）----
            if cfg.run.do_plot
                fprintf('  [步骤10] 逐帧动图\n');
                plot_beam_timeline_gif(all_raw_plots, fused_plots, beam_schedule, batch_result_dir, cfg);
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

            if ~cfg.export.keep_parse_mat
                cleanup_parse_outputs(parse_bundle, lg);
            end
    end

end

fprintf('\n========== 全部处理完成，总耗时 %.1f 分钟 ==========\n', toc(t_total) / 60);

%% 14. 本地辅助函数：路径定位与上下文构建
function raw_specs = locate_raw_inputs(data_dir, paths)
    %LOCATE_RAW_INPUTS 定位所有 RX 批次的原始输入文件。
    %
    % 输入：
    %   data_dir  - 待处理数据目录
    %   paths     - 路径配置结构体
    % 输出：
    %   raw_specs - 结构体数组，每个元素对应一个 RX 批次的路径集合
    % 注意：
    %   本函数返回所有可用批次，批次筛选由调用方（主流程）处理。
    % 作用：
    %   根据入口脚本最前面定义的路径规则，适配如下原始结构：
    %   data_dir/
    %     TX/<发射配置子目录>/lfm_tx.bin, metadata.json
    %     RX/<采集批次子目录>/cpi_*.bin, metadata.json

    tx_root_dir = fullfile(data_dir, paths.tx_root_dir);
    tx_dir_entries = dir(fullfile(tx_root_dir, paths.tx_subdir_pattern));
    tx_dir_entries = tx_dir_entries([tx_dir_entries.isdir]);
    tx_dir_entries = tx_dir_entries(~ismember({tx_dir_entries.name}, {'.', '..'}));
    if isempty(tx_dir_entries)
        error('run_batch_pipeline:MissingTxDir', '未在目录中找到 TX 配置子目录：%s', tx_root_dir);
    end
    [~, tx_dir_order] = sort({tx_dir_entries.name});
    tx_dir_entries = tx_dir_entries(tx_dir_order);

    tx_dir = '';
    tx_file = '';
    tx_meta_file = '';
    for tx_idx = 1:numel(tx_dir_entries)
        candidate_tx_dir = fullfile(tx_dir_entries(tx_idx).folder, tx_dir_entries(tx_idx).name);
        candidate_tx_file = fullfile(candidate_tx_dir, paths.tx_file_name);
        candidate_tx_meta_file = fullfile(candidate_tx_dir, paths.tx_meta_name);
        if exist(candidate_tx_file, 'file') && exist(candidate_tx_meta_file, 'file')
            tx_dir = candidate_tx_dir;
            tx_file = candidate_tx_file;
            tx_meta_file = candidate_tx_meta_file;
            break;
        end
    end
    if isempty(tx_dir)
        error('run_batch_pipeline:MissingTxFiles', ...
        'TX 目录下未找到同时包含 %s 和 %s 的配置子目录：%s', ...
        paths.tx_file_name, paths.tx_meta_name, tx_root_dir);
    end

    rx_root_dir = fullfile(data_dir, paths.rx_root_dir);
    rx_batch_entries = dir(fullfile(rx_root_dir, paths.rx_batch_pattern));
    rx_batch_entries = rx_batch_entries([rx_batch_entries.isdir]);
    rx_batch_entries = rx_batch_entries(~ismember({rx_batch_entries.name}, {'.', '..'}));
    if isempty(rx_batch_entries)
        error('run_batch_pipeline:MissingRxBatchDir', '未在目录中找到 RX 批次子目录：%s', rx_root_dir);
    end
    [~, rx_batch_order] = sort({rx_batch_entries.name});
    rx_batch_entries = rx_batch_entries(rx_batch_order);

    rx_batch_dirs = cell(1, numel(rx_batch_entries));
    rx_files = {};
    for batch_idx = 1:numel(rx_batch_entries)
        rx_batch_dirs{batch_idx} = fullfile(rx_batch_entries(batch_idx).folder, rx_batch_entries(batch_idx).name);
        rx_entries = dir(fullfile(rx_batch_dirs{batch_idx}, paths.rx_pattern));
        if isempty(rx_entries)
            continue;
        end
        rx_entry_names = {rx_entries.name};
        nums = cellfun(@(n) str2double(regexp(n, '\d+', 'match', 'once')), rx_entry_names);
        [~, ord] = sort(nums);
        rx_files = [rx_files, fullfile(rx_batch_dirs{batch_idx}, rx_entry_names(ord))]; %#ok<AGROW>
    end
    if isempty(rx_files)
        error('run_batch_pipeline:MissingCpiFiles', '未在 RX 批次目录中找到 cpi_*.bin：%s', rx_root_dir);
    end
    rx_meta_file = fullfile(rx_batch_dirs{1}, paths.rx_meta_name);
    if ~exist(rx_meta_file, 'file')
        error('run_batch_pipeline:MissingRxMetaFile', ...
        '首个 RX 批次目录中未找到元数据文件 %s：%s', paths.rx_meta_name, rx_batch_dirs{1});
    end

    raw_spec = struct();
    raw_spec.data_dir = data_dir;
    raw_spec.tx_dir = tx_dir;
    raw_spec.tx_file = tx_file;
    raw_spec.tx_meta_file = tx_meta_file;
    raw_spec.rx_root_dir = rx_root_dir;
    raw_spec.rx_batch_dirs = rx_batch_dirs;
    raw_spec.rx_meta_file = rx_meta_file;
    raw_spec.rx_files = rx_files;
    [~, first_batch_name] = fileparts(rx_batch_dirs{1});
    raw_spec.batch_name = first_batch_name;
    raw_specs = raw_spec;
end

function parse_bundle = load_latest_parse_bundle(data_dir, result_dir_name, parse_info_pattern, batch_name)
    %LOAD_LATEST_PARSE_BUNDLE 读取最近一次解析输出。
    %
    % 输入：
    %   data_dir           - 数据目录
    %   parse_info_pattern - 解析结果文件匹配模式
    % 输出：
    %   parse_bundle       - 与 batch_parse_bin 输出一致的结构体
    % 作用：
    %   当入口关闭重新解析时，直接读取当前版本生成的解析结果继续后续流程。

    if nargin < 4
        batch_name = '';
    end

    % 收集全部候选搜索目录：所有历史运行的时间戳目录（从新到旧），
    % 以及它们对应的批次子目录；最后才回退到 data_dir 本身。
    % 这样即使当前运行刚创建了空的时间戳目录，也能自动回溯到上一次有数据的结果。
    search_dirs = {};
    all_run_dirs = get_all_run_output_dirs(data_dir, result_dir_name);
    for ri = 1:numel(all_run_dirs)
        if ~isempty(batch_name)
            search_dirs{end + 1} = fullfile(all_run_dirs{ri}, batch_name); %#ok<AGROW>
        end
        search_dirs{end + 1} = all_run_dirs{ri}; %#ok<AGROW>
    end
    search_dirs{end + 1} = data_dir;

    parse_files = [];
    for si = 1:numel(search_dirs)
        if ~exist(search_dirs{si}, 'dir')
            continue;
        end
        parse_files = dir(fullfile(search_dirs{si}, parse_info_pattern));
        if ~isempty(parse_files)
            break;
        end
    end
    if isempty(parse_files)
        error('run_batch_pipeline:MissingParseInfo', '未在目录中找到 parse_info_*.mat：%s', data_dir);
    end
    [~, idx] = max([parse_files.datenum]);
    parse_file = fullfile(parse_files(idx).folder, parse_files(idx).name);
    S = load(parse_file, 'rx_param', 'tx');
    parse_bundle = struct();
    parse_bundle.rx_param = S.rx_param;
    parse_bundle.tx = S.tx;
    parse_bundle.parse_ts = regexp(parse_files(idx).name, '\d{8}_\d{6}', 'match', 'once');
    parse_bundle.parse_info_file = parse_file;
    parse_bundle.data_dir = data_dir;
    parse_bundle.channel_ids = builtin('double', S.rx_param.channels(:).');
    parse_bundle.channel_var_names = cell(1, numel(parse_bundle.channel_ids));
    parse_bundle.rx_channel_files = cell(1, numel(parse_bundle.channel_ids));
    for ci = 1:numel(parse_bundle.channel_ids)
        ch_id = parse_bundle.channel_ids(ci);
        parse_bundle.channel_var_names{ci} = sprintf('rx_ch%d', ch_id);
        ch_entries = dir(fullfile(parse_files(idx).folder, sprintf('rx_ch%d_%s.mat', ch_id, parse_bundle.parse_ts)));
        if isempty(ch_entries)
            ch_entries = dir(fullfile(data_dir, sprintf('rx_ch%d_%s.mat', ch_id, parse_bundle.parse_ts)));
        end
        if isempty(ch_entries)
            error('run_batch_pipeline:MissingChannelMat', ...
            '未找到通道 ch%d 对应的解析 mat 文件，时间戳=%s，目录=%s', ch_id, parse_bundle.parse_ts, data_dir);
        end
        [~, ch_idx] = max([ch_entries.datenum]);
        parse_bundle.rx_channel_files{ci} = fullfile(ch_entries(ch_idx).folder, ch_entries(ch_idx).name);
    end
end


function all_run_dirs = get_all_run_output_dirs(data_dir, result_dir_name)
    %GET_ALL_RUN_OUTPUT_DIRS 获取全部历史运行的时间戳目录（从新到旧）。
    %
    % 输入：
    %   data_dir         - 数据目录
    %   result_dir_name  - 结果根目录名称
    % 输出：
    %   all_run_dirs     - 全部时间戳目录路径 cell 数组；若不存在则返回空 cell
    % 作用：
    %   在关闭重算、需要复用已有输出时，按时间倒序扫描所有历史运行目录，
    %   优先匹配最近一次包含目标文件的运行结果。

    all_run_dirs = {};
    result_root_dir = fullfile(data_dir, result_dir_name);
    if ~exist(result_root_dir, 'dir')
        return;
    end
    entries = dir(result_root_dir);
    entries = entries([entries.isdir]);
    names = {entries.name};
    mask = ~ismember(names, {'.', '..'});
    entries = entries(mask);
    names = names(mask);
    ts_mask = ~cellfun('isempty', regexp(names, '^\d{8}_\d{6}$', 'once'));
    entries = entries(ts_mask);
    if isempty(entries)
        return;
    end
    [~, idx] = sort([entries.datenum], 'descend');
    entries = entries(idx);
    all_run_dirs = cell(1, numel(entries));
    for i = 1:numel(entries)
        all_run_dirs{i} = fullfile(entries(i).folder, entries(i).name);
    end
end

function cleanup_parse_outputs(parse_bundle, status_cb)
    %CLEANUP_PARSE_OUTPUTS 删除本次运行生成的解析 mat 文件。
    %
    % 输入：
    %   parse_bundle - 解析阶段输出结构体
    %   status_cb    - 状态输出函数
    % 输出：
    %   无
    % 作用：
    %   当不希望长期保留解析缓存时，删除 rx_ch*.mat 与 parse_info_*.mat，减少磁盘占用。

    for ci = 1:numel(parse_bundle.rx_channel_files)
        this_file = parse_bundle.rx_channel_files{ci};
        if exist(this_file, 'file')
            delete(this_file);
            status_cb(sprintf('  [清理] 已删除解析通道文件：%s', this_file));
        end
    end
    if isfield(parse_bundle, 'parse_info_file') && exist(parse_bundle.parse_info_file, 'file')
        delete(parse_bundle.parse_info_file);
        status_cb(sprintf('  [清理] 已删除解析索引文件：%s', parse_bundle.parse_info_file));
    end
end


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
    rd_ctx.pw_samples = sum(abs(tx.data) > 0.01 * max(abs(tx.data)));
    ref_freq = fft(single(tx.data), rd_ctx.pri_len);
    rd_ctx.conj_ref_freq = conj(ref_freq) .* single(hamming(rd_ctx.pri_len));
end


function plot_beam_timeline_gif(all_raw_plots, fused_plots, beam_schedule, result_dir, cfg)
%PLOT_BEAM_TIMELINE_GIF 逐扫描融合后目标动图。
% 使用 fused_plots（融合后）按 scan_id 分帧，合成为 GIF。
% fused_plots 列: [r(m), az(deg), el(deg), vr(m/s), time(s), scan_id]

if isempty(fused_plots) || size(fused_plots, 2) < 6
    fprintf('[时间线GIF] fused_plots 缺少 scan_id 列，回退到原始点迹。\n');
    if isempty(all_raw_plots), return; end
    % 回退：画原始点迹（按 scan_id 列=8）
    scan_ids = unique(all_raw_plots(:, 8));
    use_fused = false;
else
    scan_ids = unique(fused_plots(:, 6));
    use_fused = true;
end

n_frames = numel(scan_ids);
if n_frames < 1
    fprintf('[时间线GIF] 无有效帧，跳过。\n');
    return;
end

fprintf('[时间线GIF] 生成 %d 帧动图（融合后目标）...\n', n_frames);

fig = figure('Visible', 'off', 'Position', [100, 100, 800, 600]);
gif_file = fullfile(result_dir, sprintf('Timeline_%s.gif', ...
    char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'))));

delay = cfg.export.gif_delay;
r_range = cfg.plot.range_window_m;
v_range = cfg.plot.velocity_window_mps;

for fi = 1:n_frames
    clf;
    sid = scan_ids(fi);

    if use_fused
        mask = fused_plots(:, 6) == sid;
        frame = fused_plots(mask, :);
        % 列: [r, az, el, vr, time, scan_id]
        r_data = frame(:, 1);
        v_data = frame(:, 4);
        marker_sz = 50;  % 融合后目标用统一大小
    else
        mask = all_raw_plots(:, 8) == sid;
        frame = all_raw_plots(mask, :);
        r_data = frame(:, 1);
        v_data = frame(:, 4);
        pwr_data = frame(:, 6);
        marker_sz = max(15, pwr_data - min(pwr_data) + 15);
    end

    if isempty(frame), continue; end

    scatter(v_data, r_data, marker_sz, 'b', 'filled');
    xlim(v_range);
    ylim(r_range);
    xlabel('Radial Velocity (m/s)');
    ylabel('Range (m)');
    title(sprintf('Scan %d/%d  N=%d', fi, n_frames, size(frame, 1)));
    grid on;
    box on;

    drawnow;

    frame_img = getframe(fig);
    im = frame2im(frame_img);
    [A, map] = rgb2ind(im, 256);
    if fi == 1
        imwrite(A, map, gif_file, 'gif', 'LoopCount', inf, 'DelayTime', delay);
    else
        imwrite(A, map, gif_file, 'gif', 'WriteMode', 'append', 'DelayTime', delay);
    end
end

close(fig);
fprintf('[时间线GIF] 已保存：%s\n', gif_file);
end
