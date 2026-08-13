function out_file = process_rd_beam(beam_id, beam_az, beam_el, data_dir, parse_bundle, rd_ctx, preproc_state, process_cfg, result_dir, status_cb)
%PROCESS_RD_BEAM 逐波位 RD 处理。
%
% 输入：
%   beam_id       - 波位编号（1-based）
%   beam_az       - 波位方位角（度）
%   beam_el       - 波位俯仰角（度）
%   data_dir      - 数据目录
%   parse_bundle  - 解析阶段输出的结构体
%   rd_ctx        - RD 上下文（已按波位参数调整：n_cpi=256, n_overlap=0）
%   preproc_state - 预处理初始化状态（跨波位共享的对齐参数）
%   process_cfg   - RD 处理配置结构体
%   result_dir    - 结果输出目录（批次级）
%   status_cb     - 状态输出函数
% 输出：
%   out_file      - 生成的 RD_Proc_beam*_*.mat 完整路径
% 作用：
%   从 flat 通道 mat 文件中提取波位 beam_id 对应的脉冲段，
%   各段独立做距离压缩与慢时间 FFT，写入逐波位 RD 结果文件。

if nargin < 10 || isempty(status_cb)
    status_cb = @(msg) fprintf('%s\n', msg);
end

% ---- 准备输出目录与文件 ----
beam_result_dir = fullfile(result_dir, sprintf('beam_%03d', beam_id));
if ~exist(beam_result_dir, 'dir')
    mkdir(beam_result_dir);
end

[~, dataset_name] = fileparts(data_dir);
ts_out = char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'));
out_file = fullfile(beam_result_dir, sprintf('RD_Proc_beam%03d_%s_%s.mat', beam_id, dataset_name, ts_out));

channel_ids = parse_bundle.channel_ids;
if isempty(channel_ids)
    channel_ids = parse_bundle.rx_param.channels;
end
channel_files = parse_bundle.rx_channel_files;
channel_var_names = parse_bundle.channel_var_names;
if isempty(channel_var_names)
    channel_var_names = arrayfun(@(id) sprintf('rx_ch%d', id), channel_ids, 'UniformOutput', false);
end
if numel(channel_files) ~= numel(channel_ids)
    error('process_rd_beam:InvalidChannelFiles', '通道文件数量与通道编号数量不一致。');
end

% 输出变量命名（与 process_rd 保持一致）
output_names = cell(1, numel(channel_ids));
for i = 1:numel(channel_ids)
    output_names{i} = sprintf('RD_Ch%d_All', channel_ids(i));
end
if numel(output_names) >= 3
    output_names{1} = 'RD_Sum_All';
    output_names{2} = 'RD_Az_All';
    output_names{3} = 'RD_El_All';
end

mf = matfile(out_file, 'Writable', true);
mf.r_axis_full = rd_ctx.r_axis_full;
mf.v_axis_full = rd_ctx.v_axis_full;
mf.N_cpi = int32(rd_ctx.n_cpi);
mf.beam_id = int32(beam_id);
mf.beam_az = single(beam_az);
mf.beam_el = single(beam_el);
mf.processing_meta = build_processing_meta(rd_ctx, process_cfg, channel_ids);
status_cb(sprintf('[RD Beam %d] 输出文件：%s', beam_id, out_file));

% ---- 帧间间隔与帧数 ----
pulses_per_dwell = rd_ctx.n_cpi;
pri_len = rd_ctx.pri_len;
pulses_per_scan = rd_ctx.pulses_per_scan;
total_frames = rd_ctx.total_blocks;

% ---- 逐通道处理 ----
shared_preproc_state = preproc_state;

for channel_idx = 1:numel(channel_ids)
    channel_id = channel_ids(channel_idx);
    channel_file = channel_files{channel_idx};
    channel_var_name = channel_var_names{channel_idx};

    if ~exist(channel_file, 'file')
        error('process_rd_beam:MissingChannelFile', '缺少通道数据文件：%s', channel_file);
    end

    channel_preproc_state = reset_channel_state(shared_preproc_state);
    estimate_alignment = (channel_idx == 1);

    % Flat 格式：每个通道文件是独立 ~2.3GB mat，变量在顶层
    mf_rx = matfile(channel_file);

    n_blocks_written = 0;
    last_phase = 0;

    % 轻量预分配：仅写一个远角元素来固定 3D 形状
    mf.(output_names{channel_idx})(1, 1, total_frames) = complex(single(0), single(0));

    for k = 1:total_frames
        skip_pri = 0;
        if isfield(rd_ctx, 'skip_pri'), skip_pri = rd_ctx.skip_pri; end
        if isfield(rd_ctx, 'initial_scan_pri') && rd_ctx.initial_scan_pri ~= 0
            pri_base = rd_ctx.initial_scan_pri + (k - 1) * pulses_per_scan;
        else
            pri_base = (k - 1) * pulses_per_scan;
        end
        start_pri = skip_pri + pri_base + rd_ctx.beam_start_offset + 1;
        end_pri   = start_pri + pulses_per_dwell - 1;
        N_cur = pulses_per_dwell;

        samp_s = (start_pri - 1) * pri_len + 1;
        samp_e = end_pri * pri_len;

        total_samples_in_mat = double(parse_bundle.rx_param.total_samples);
        if samp_s < 1 || samp_e > total_samples_in_mat
            status_cb(sprintf('[RD Beam %d] 扫描 %d 数据不完整（samp %d:%d / %d），跳过', ...
                beam_id, k, samp_s, samp_e, total_samples_in_mat));
            continue;
        end

        % 波位归属验证
        if isfield(parse_bundle, 'beam_meta') && ~isempty(parse_bundle.beam_meta)
            bm = parse_bundle.beam_meta;
            check_pri = [start_pri, start_pri + floor(pulses_per_dwell/2), end_pri];
            for pi = 1:numel(check_pri)
                pi_idx = check_pri(pi);
                if pi_idx > numel(bm.meta_valid) || ~bm.meta_valid(pi_idx)
                    continue;
                end
                az_ok = abs(double(bm.az_deg(pi_idx)) - beam_az) < 0.5;
                el_ok = abs(double(bm.el_deg(pi_idx)) - beam_el) < 0.5;
                if ~az_ok || ~el_ok
                    error('process_rd_beam:BeamMixing', ...
                        '[RD Beam %d] 扫描 %d PRI=%d: 元数据角度=(%.2f, %.2f), 期望=(%.2f, %.2f)', ...
                        beam_id, k, pi_idx, ...
                        double(bm.az_deg(pi_idx)), double(bm.el_deg(pi_idx)), ...
                        beam_az, beam_el);
                end
            end
        end

        % Flat 格式：顶层变量直接访问
        cur = reshape(single(mf_rx.(channel_var_name)(samp_s:samp_e, 1)), pri_len, N_cur);
        dw_shift = mod(double(channel_preproc_state.dw_offset), pri_len);
        if dw_shift ~= 0
            cur = cur([dw_shift+1:pri_len, 1:dw_shift], :);
        end

        % 预处理（距离压缩 + 对齐 + 频偏补偿）
        [PC_corr, channel_preproc_state] = preprocess( ...
            'chunk', cur, rd_ctx, process_cfg.preprocess, channel_preproc_state, ...
            estimate_alignment, k, last_phase, status_cb);
        last_phase = channel_preproc_state.last_phase;

        % MTI（两脉冲相消）
        if process_cfg.process.do_mti_twopulse
            proc_blk = PC_corr(:, 2:end) - PC_corr(:, 1:end - 1);
        else
            proc_blk = PC_corr;
        end

        % 慢时间 FFT
        n_v = size(proc_blk, 2);
        rd_block = fftshift(fft(proc_blk .* single(hamming(n_v).'), rd_ctx.n_cpi, 2), 2);

        % 零多普勒清除
        if isfield(rd_ctx, 'zero_doppler_cells') && rd_ctx.zero_doppler_cells >= 0
            nz = rd_ctx.zero_doppler_cells;
            dc_bin = rd_ctx.n_cpi / 2 + 1;
            z1 = max(1, dc_bin - nz);
            z2 = min(rd_ctx.n_cpi, dc_bin + nz);
            rd_block(:, z1:z2) = 0;
        end

        % 写盘
        n_blocks_written = n_blocks_written + 1;
        mf.(output_names{channel_idx})(1:rd_ctx.max_calc_samples, 1:rd_ctx.n_cpi, n_blocks_written) = ...
            rd_block(1:rd_ctx.max_calc_samples, :);

        clear cur PC_corr proc_blk rd_block;
    end

    % 跨通道共享对齐量
    if channel_idx == 1
        shared_preproc_state.global_delta_p = channel_preproc_state.global_delta_p;
        shared_preproc_state.freq_offsets = channel_preproc_state.freq_offsets;
        shared_preproc_state.phase_starts = channel_preproc_state.phase_starts;
    end

    if channel_idx == numel(channel_ids)
        mf.total_blocks = int32(n_blocks_written);
    end

    clear mf_rx;
    status_cb(sprintf('[RD Beam %d] 通道 ch%d 完成，输出 %d 帧', beam_id, channel_id, n_blocks_written));
end

clear mf;
status_cb(sprintf('[RD Beam %d] 全部通道处理完成：%s', beam_id, out_file));
end

function preproc_state = reset_channel_state(preproc_state)
preproc_state.phase_starts = zeros(size(preproc_state.phase_starts), 'like', preproc_state.phase_starts);
preproc_state.last_phase = 0;
end

function meta = build_processing_meta(rd_ctx, process_cfg, channel_ids)
meta = struct();
meta.channel_roles = struct( ...
    'sum', channel_ids(1), ...
    'az_diff', sprintf('channel%d_left_minus_right', channel_ids(2)), ...
    'el_diff', sprintf('channel%d_up_minus_down', channel_ids(3)));
meta.range_bin_spacing_m = rd_ctx.c / (2 * rd_ctx.fs);
meta.mti = struct('two_pulse_cancel', process_cfg.process.do_mti_twopulse);
meta.preprocessing = struct('fast_time_dc_remove', process_cfg.preprocess.do_fast_dc_remove);
end
