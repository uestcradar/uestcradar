%RUN_BATCH_PIPELINE Matlab_Helium 批处理总入口脚本。
%
% 本脚本采用“从上到下”的一页式流程组织方式：
% 1. 先在脚本最前面集中设置全部路径、开关和算法参数
% 2. 再按顺序执行“原始数据定位 -> 解析 -> 预处理 -> RD -> 检测 -> 聚类 -> 测角 -> 保存 -> 绘图”
% 3. 所有子模块都只负责单一环节，主流程顺序统一在本脚本中清晰展示

cfg = struct();

%% 1. 参数区：输入与输出路径
cfg.paths.data_folders = { ...
    % 'E:\通感\外场试验\202606_温江_非自制天线_硬件组自己飞\0610数据\LFM_20M_64_1024' ...
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
cfg.paths.rd_pattern = 'RD_Proc_*.mat'; % 已有 RD 结果文件匹配规则；关闭 RD 计算时用它定位最新结果。

%% 2. 参数区：运行开关
cfg.run.do_parse = true; % 是否重新解析原始 bin 文件；true 表示重新生成 rx_ch*.mat 和 parse_info_*.mat。
cfg.run.do_process = true; % 是否重新执行 RD 处理；false 表示直接复用已有 RD_Proc_*.mat。
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
cfg.rd.n_cpi = 512; % 每个 RD 块使用的慢时间脉冲数；决定多普勒分辨率与积累长度。
cfg.rd.n_overlap = 256; % 相邻 RD 块的重叠脉冲数；数值越大，时间连续性越好但计算量也越高。
cfg.rd.max_range_m = 2000; % 最大处理距离，单位米；只保留该距离以内的距离单元参与后续处理。
cfg.rd.frames_per_chunk = 4096; % 单次从 mat 文件中读取的慢时间帧数；用于平衡内存占用与处理效率。
cfg.rd.do_mti_mean = true; % 是否在每个 CPI 内做慢时间均值消除；用于抑制零多普勒静态杂波。
cfg.rd.do_mti_twopulse = true; % 是否执行两脉冲相消；用于进一步增强运动目标、压制静态背景。

%% 6. 参数区：目标检测参数
cfg.detect.range_window_m = [300, 800]; % 检测阶段使用的距离显示/分析范围，单位米。
cfg.detect.velocity_window_mps = [-50, 50]; % 检测阶段使用的速度显示/分析范围，单位米每秒。
cfg.detect.cfar_guard_r = 2; % CFAR 在距离维的保护单元数；避免参考窗污染目标主瓣。
cfg.detect.cfar_guard_d = 4; % CFAR 在速度维的保护单元数；避免参考窗污染目标主瓣。
cfg.detect.cfar_ref_r = 8; % CFAR 在距离维的参考单元数；用于估计局部噪声背景。
cfg.detect.cfar_ref_d = 16; % CFAR 在速度维的参考单元数；用于估计局部噪声背景。
cfg.detect.cfar_pfa = 1e-6; % CFAR 虚警概率；这里设置为 10^-6，目的是进一步压低虚警点数量，让检测结果更保守。

%% 7. 参数区：聚类参数
cfg.cluster.dbscan_eps = 3; % DBSCAN 邻域半径；单位是“距离 bin / 速度 bin”的索引尺度。
cfg.cluster.dbscan_min = 3; % DBSCAN 成簇最少点数；低于该点数的检测点会被视为噪声或孤立点。

%% 8. 参数区：测角参数
cfg.angle.k_mono = 1.0; % 单脉冲比幅系数缩放因子；用于把差比值映射到角度刻度。
cfg.angle.range_window_m = [300, 800]; % 测角阶段保留目标的距离范围，单位米。
cfg.angle.velocity_window_mps = [-50, 50]; % 测角阶段保留目标的速度范围，单位米每秒。
cfg.angle.min_display_power_dB = 110; % 测角时参与输出的最小显示功率阈值，单位 dB。

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
        fprintf('  [批次 %d/%d] %s
', bi, numel(raw_specs), raw_spec.batch_name);
        fprintf('    [输出] %s
', batch_result_dir);
    
    %% 13.2 解析原始数据
    if cfg.run.do_parse
        fprintf('  [步骤2] 重新解析原始 bin 数据\n');
        parse_bundle = batch_parse_bin(raw_spec);
    else
        fprintf('  [步骤2] 读取已有解析结果\n');
        result_dir_name = cfg.paths.result_dir_name;
        parse_info_pattern = cfg.paths.parse_info_pattern;
        parse_bundle = load_latest_parse_bundle(data_dir, result_dir_name, parse_info_pattern);
    end
    
    %% 13.3 构建 RD 处理上下文
    fprintf('  [步骤3] 构建 RD 处理上下文\n');
    rd_ctx = build_rd_context(parse_bundle.rx_param, parse_bundle.tx, cfg.rd, cfg.radar);
    if rd_ctx.total_blocks <= 0
        error('run_batch_pipeline:InsufficientPulses', '有效脉冲数不足，无法形成一个完整的 CPI 块。');
    end
    lg = cfg.runtime.status_cb;
    lg(sprintf('  [上下文] PRI 点数=%d，采样率=%.2f MHz，PRF=%.0f Hz', ...
        rd_ctx.pri_len, rd_ctx.fs / 1e6, 1 / rd_ctx.prt));
    
    %% 13.4 初始化预处理
    fprintf('  [步骤4] 初始化预处理模块\n');
    % 这里先执行 preprocess('init')，目的不是立刻把整批数据全部预处理完，
    % 而是先完成“只需要做一次”的准备工作，例如直达波定位、预处理状态初始化、
    % 以及后续 chunk 循环要复用的缓存量准备。
    % 这样做的原因是：原始数据量通常较大，真正的预处理必须在 RD 阶段按 chunk 分块进行，
    % 避免一次性把所有数据读入内存，也保证每个数据块都沿用同一套初始化结果。
    [~, preproc_state] = preprocess('init', data_dir, parse_bundle.tx, rd_ctx, cfg.preprocess, lg);
    lg(sprintf('  [预处理] 直达波 bin=%d，对齐模式=%s', preproc_state.dw_bin, preproc_state.dw_mode));
    
    %% 13.5 执行 RD 处理
    if cfg.run.do_process
        fprintf('  [步骤5] 执行 RD 处理\n');
        % 这里才真正进入整批数据的主体计算阶段：
        % 前面 init 只是准备预处理状态，这里会按通道、按 chunk 逐块完成
        % 距离压缩、慢时间处理和 RD 结果写盘。
        process_cfg = struct();
        process_cfg.process = cfg.rd;
        process_cfg.preprocess = cfg.preprocess;
        out_file = process_rd(data_dir, parse_bundle, rd_ctx, preproc_state, process_cfg, result_dir, lg);
    else
        fprintf('  [步骤5] 读取已有 RD 结果\n');
        result_dir_name = cfg.paths.result_dir_name;
        rd_pattern = cfg.paths.rd_pattern;
        out_file = load_latest_rd_result(data_dir, result_dir_name, rd_pattern);
    end
    
    %% 13.6 执行目标检测
    fprintf('  [步骤6] 执行目标检测\n');
    % 这里先把 RD 结果裁剪到我们真正关心的距离窗和速度窗，
    % 目的是减少后续检测、聚类和测角的无关运算量，也让分析范围更聚焦。
    rd = matfile(out_file);
    r_axis = builtin('double', rd.r_axis_full);
    v_axis = builtin('double', rd.v_axis_full);
    n_frames = builtin('double', rd.total_blocks);
    
    detect_r_range = cfg.detect.range_window_m;
    detect_v_range = cfg.detect.velocity_window_mps;
    r_mask = r_axis >= detect_r_range(1) & r_axis <= detect_r_range(2);
    v_mask = v_axis >= detect_v_range(1) & v_axis <= detect_v_range(2);
    r_disp = r_axis(r_mask);
    v_disp = v_axis(v_mask);
    frame_ids = 1:cfg.plot.frame_step:n_frames;
    nf = numel(frame_ids);
    
    cfar_p = struct();
    cfar_p.guard_r = cfg.detect.cfar_guard_r;
    cfar_p.guard_d = cfg.detect.cfar_guard_d;
    cfar_p.ref_r = cfg.detect.cfar_ref_r;
    cfar_p.ref_d = cfg.detect.cfar_ref_d;
    cfar_p.pfa = cfg.detect.cfar_pfa;
    
    [~, rd_name] = fileparts(out_file);
    det_mat = fullfile(result_dir, sprintf('%s_det.mat', rd_name));
    ang_mat = fullfile(result_dir, sprintf('%s_angle.mat', rd_name));
    
    lg(sprintf('  [检测] RD 总帧数=%d，抽样绘图帧数=%d', n_frames, nf));
    lg(sprintf('  [检测] 分析距离单元数=%d，分析速度单元数=%d', numel(r_disp), numel(v_disp)));
    
    det_result = struct();
    det_result.rd_file = out_file;
    det_result.frame_ids = frame_ids;
    det_result.n_frames = n_frames;
    det_result.r_disp = r_disp;
    det_result.v_disp = v_disp;
    det_result.r_range = detect_r_range;
    det_result.v_range = detect_v_range;
    det_result.det_r = cell(1, nf);
    det_result.det_v = cell(1, nf);
    det_result.clu_ids = cell(1, nf);
    det_result.n_clu = zeros(1, nf, 'int32');
    det_result.cfar_p = cfar_p;
    det_result.db_eps = cfg.cluster.dbscan_eps;
    det_result.db_min = cfg.cluster.dbscan_min;
    
    %% 13.7 执行聚类
    fprintf('  [步骤7] 执行目标聚类\n');
    % 聚类单独作为一步，是因为 CFAR 给出的只是离散检测点，
    % 这些点往往对应同一个目标的多个相邻单元。通过 DBSCAN 聚类，
    % 后面测角时就可以按“目标簇”而不是按“单个像素点”来组织结果。
    has_az = ismember('RD_Az_All', who(rd));
    has_el = ismember('RD_El_All', who(rd));
    
    ang_result = struct();
    ang_result.rd_file = out_file;
    ang_result.frame_ids = frame_ids;
    ang_result.n_frames = n_frames;
    ang_result.r_disp = r_disp;
    ang_result.v_disp = v_disp;
    ang_result.r_range = detect_r_range;
    ang_result.v_range = detect_v_range;
    ang_result.r_m = cell(1, nf);
    ang_result.v_m = cell(1, nf);
    ang_result.az_m = cell(1, nf);
    ang_result.el_m = cell(1, nf);
    ang_result.sz_m = cell(1, nf);
    ang_result.ang_r_rng = cfg.angle.range_window_m;
    ang_result.ang_v_rng = cfg.angle.velocity_window_mps;
    ang_result.k_mono = cfg.angle.k_mono;
    ang_result.has_angle = cfg.run.do_angle && has_az && has_el;
    
    %% 13.8 执行测角
    fprintf('  [步骤8] 执行测角与目标属性整理\n');
    % 这里把检测、聚类和测角放在同一个逐帧循环里，
    % 目的是让每一帧的目标分析结果在同一处完成收口：
    % 先得到候选点，再聚成目标簇，最后从每个簇里选代表点做测角。
    for fi = 1:nf
        k = frame_ids(fi);
        rd0 = rd.RD_Sum_All(:, :, k);
        rd_sub = rd0(r_mask, v_mask);
        pwr = abs(rd_sub).^2;
        
        if cfg.run.do_detect
            det_mask = cfar_2d(pwr, cfar_p);
            [det_r_idx, det_v_idx] = find(det_mask);
        else
            det_r_idx = zeros(0, 1);
            det_v_idx = zeros(0, 1);
        end
        n_det = numel(det_r_idx);
        
        if cfg.run.do_cluster && n_det >= cfg.cluster.dbscan_min
            [clu_ids, n_clu] = dbscan_cluster([det_r_idx, det_v_idx], cfg.cluster.dbscan_eps, cfg.cluster.dbscan_min);
        else
            clu_ids = zeros(n_det, 1, 'int32');
            n_clu = 0;
        end
        
        det_result.det_r{fi} = uint16(det_r_idx);
        det_result.det_v{fi} = uint16(det_v_idx);
        det_result.clu_ids{fi} = clu_ids;
        det_result.n_clu(fi) = int32(n_clu);
        
        if ang_result.has_angle
            rd1 = rd.RD_Az_All(:, :, k);
            rd2 = rd.RD_El_All(:, :, k);
            rd1_sub = rd1(r_mask, v_mask);
            rd2_sub = rd2(r_mask, v_mask);
            az_ratio = real(rd1_sub ./ (rd_sub + eps));
            el_ratio = real(rd2_sub ./ (rd_sub + eps));
            
            [r_m, v_m, az_m, el_m, sz_m] = mono_angle( ...
                r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
                pwr, az_ratio, el_ratio, cfg.angle.k_mono, ...
                cfg.angle.range_window_m, cfg.angle.velocity_window_mps, cfg.angle.min_display_power_dB);
            ang_result.r_m{fi} = single(r_m);
            ang_result.v_m{fi} = single(v_m);
            ang_result.az_m{fi} = single(az_m);
            ang_result.el_m{fi} = single(el_m);
            ang_result.sz_m{fi} = single(sz_m);
        end
        
        if mod(fi, 200) == 0 || fi == nf
            lg(sprintf('  [目标分析] 已完成 %d / %d 帧', fi, nf));
        end
    end
    
    %% 13.9 保存分析结果
    fprintf('  [步骤9] 保存分析结果\n');
    % 检测结果和测角结果单独保存的原因是：
    % 后续如果只调整绘图样式、标注方式或结果筛选条件，
    % 就可以直接复用这些分析结果，而不必重新跑前面的 RD 和目标分析。
    target_result = struct();
    target_result.det_result = det_result;
    target_result.ang_result = ang_result;
    target_result.det_result_file = det_mat;
    target_result.ang_result_file = ang_mat;
    target_result.has_angle = ang_result.has_angle;
    
    if cfg.export.save_analysis_mat
        save(det_mat, 'det_result', '-v7.3');
        lg(sprintf('  [保存] 检测结果已写入：%s', det_mat));
        if ang_result.has_angle
            save(ang_mat, 'ang_result', '-v7.3');
            lg(sprintf('  [保存] 测角结果已写入：%s', ang_mat));
        end
    else
        target_result.det_result_file = '';
        target_result.ang_result_file = '';
    end
    
    %% 13.10 执行绘图与导出
    if cfg.run.do_plot
        fprintf('  [步骤10] 执行绘图与导出\n');
        % 绘图放在最后单独执行，目的是让可视化完全消费前面已经整理好的结果，
        % 这样一旦后续想改单独的图像风格、抽帧步长或显示窗口，就不会影响前面的算法主流程。
        plot_opts = struct();
        plot_opts.r_range = cfg.plot.range_window_m;
        plot_opts.v_range = cfg.plot.velocity_window_mps;
        plot_opts.clim_dB = cfg.plot.clim_dB;
        plot_opts.frame_step = cfg.plot.frame_step;
        plot_opts.delay = cfg.export.gif_delay;
        plot_opts.k_mono = cfg.angle.k_mono;
        plot_opts.status_cb = cfg.runtime.status_cb;
        radar_plot(out_file, result_dir, plot_opts, target_result.det_result, target_result.ang_result);
    end
    
    if ~cfg.export.keep_rd_mat && exist(out_file, 'file')
        delete(out_file);
        lg(sprintf('  [清理] 已删除 RD 文件：%s', out_file));
    end
    
    if ~cfg.export.keep_parse_mat
        cleanup_parse_outputs(parse_bundle, lg);
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

search_dirs = {data_dir};
latest_run_dir = get_latest_run_output_dir(data_dir, result_dir_name);
if ~isempty(latest_run_dir)
    if ~isempty(batch_name)
        search_dirs = [{fullfile(latest_run_dir, batch_name)}, {latest_run_dir}, search_dirs];
    else
        search_dirs = [{latest_run_dir}, search_dirs];
    end
end
parse_files = [];
for si = 1:numel(search_dirs)
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

function rd_file = load_latest_rd_result(data_dir, result_dir_name, rd_pattern, batch_name)
%LOAD_LATEST_RD_RESULT 读取最近一次 RD 结果文件。
%
% 输入：
%   data_dir    - 数据目录
%   result_dir  - 结果目录
%   rd_pattern  - RD 结果文件匹配模式
% 输出：
%   rd_file     - 最近一次 RD 结果文件绝对路径
% 作用：
%   当入口关闭 RD 计算时，直接复用已有 RD 结果进入后续分析或绘图。

if nargin < 4
    batch_name = '';
end

search_dirs = {};
latest_run_dir = get_latest_run_output_dir(data_dir, result_dir_name);
if ~isempty(latest_run_dir)
    if ~isempty(batch_name)
        search_dirs{end + 1} = fullfile(latest_run_dir, batch_name);
    end
    search_dirs{end + 1} = latest_run_dir;
end
search_dirs{end + 1} = fullfile(data_dir, result_dir_name);
search_dirs{end + 1} = data_dir;
rd_files = [];
for si = 1:numel(search_dirs)
    if exist(search_dirs{si}, 'dir')
        rd_files = dir(fullfile(search_dirs{si}, rd_pattern));
        if ~isempty(rd_files)
            break;
        end
    end
end
if isempty(rd_files)
    error('run_batch_pipeline:MissingRDResult', '未在目录中找到 RD_Proc_*.mat：%s', data_dir);
end
[~, idx] = max([rd_files.datenum]);
rd_file = fullfile(rd_files(idx).folder, rd_files(idx).name);
end

function latest_run_dir = get_latest_run_output_dir(data_dir, result_dir_name)
%GET_LATEST_RUN_OUTPUT_DIR 获取最近一次运行的时间戳结果目录。
%
% 输入：
%   data_dir         - 数据目录
%   result_dir_name  - 结果根目录名称
% 输出：
%   latest_run_dir   - 最近一次运行的时间戳目录；若不存在则返回空字符向量
% 作用：
%   在关闭重算、需要复用已有输出时，优先定位最近一次的独立运行目录。

latest_run_dir = '';
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
[~, idx] = max([entries.datenum]);
latest_run_dir = fullfile(entries(idx).folder, entries(idx).name);
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

function name_out = local_make_valid_name(name_in)
%LOCAL_MAKE_VALID_NAME 将 RX 批次名转为文件系统安全的名称。
name_out = regexprep(name_in, [^\\w-], _);
if isempty(name_out)
    name_out = rx_batch;
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
rd_ctx.n_cpi = builtin('double', rd_cfg.n_cpi);
rd_ctx.n_overlap = builtin('double', rd_cfg.n_overlap);
rd_ctx.n_step = rd_ctx.n_cpi - rd_ctx.n_overlap;
if rd_ctx.n_step <= 0
    error('run_batch_pipeline:InvalidOverlap', 'n_overlap 必须小于 n_cpi。');
end
rd_ctx.frames_per_chunk = builtin('double', rd_cfg.frames_per_chunk);
r_res = radar_cfg.c / (2 * rd_ctx.fs);
rd_ctx.max_calc_samples = min(round(rd_cfg.max_range_m / r_res), rd_ctx.pri_len);
rd_ctx.r_axis_full = (0:rd_ctx.max_calc_samples - 1) * r_res;
v_res = (rd_ctx.lambda / rd_ctx.prt) / (2 * rd_ctx.n_cpi);
rd_ctx.v_axis_full = (-rd_ctx.n_cpi / 2 : rd_ctx.n_cpi / 2 - 1) * v_res;
rd_ctx.v_res = v_res;
rd_ctx.effective_frames = rd_ctx.total_frames_global - 1;
rd_ctx.total_blocks = floor((rd_ctx.effective_frames - rd_ctx.n_cpi) / rd_ctx.n_step) + 1;
rd_ctx.num_chunks = ceil(rd_ctx.effective_frames / rd_ctx.frames_per_chunk);
rd_ctx.pw_samples = sum(abs(tx.data) > 0.01 * max(abs(tx.data)));
ref_freq = fft(single(tx.data), rd_ctx.pri_len);
rd_ctx.conj_ref_freq = conj(ref_freq) .* single(hamming(rd_ctx.pri_len));
end
