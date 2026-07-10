function out_file = process_rd(data_dir, parse_bundle, rd_ctx, preproc_state, process_cfg, result_dir, status_cb)
%PROCESS_RD 距离-多普勒处理模块。
%
% 输入：
%   data_dir      - 数据目录
%   parse_bundle  - 解析阶段输出的结构体
%   rd_ctx        - RD 上下文
%   preproc_state - 预处理初始化后的状态
%   process_cfg   - RD 处理配置结构体，主要使用 process_cfg.process 和 process_cfg.preprocess
%   result_dir    - 结果输出目录
%   status_cb     - 状态输出函数
% 输出：
%   out_file      - 生成的 RD_Proc_*.mat 文件路径
% 作用：
%   按通道执行“读取 -> chunk 预处理 -> CPI 组块 -> 慢时间处理 -> 写结果”。
%   单独保留这个模块的原因是：RD 处理是整条流程里最重的主体计算阶段，
%   把它集中后，主程序可以只负责调度顺序，而这里专门负责大规模数据运算与写盘。

if nargin < 7 || isempty(status_cb)
    status_cb = @(msg) fprintf('%s\n', msg);
end

if ~exist(result_dir, 'dir')
    mkdir(result_dir);
end

[~, dataset_name] = fileparts(data_dir);
ts_out = char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'));
out_file = fullfile(result_dir, sprintf('RD_Proc_%s_%s.mat', dataset_name, ts_out));

mf = matfile(out_file, 'Writable', true);
mf.r_axis_full = rd_ctx.r_axis_full;
mf.v_axis_full = rd_ctx.v_axis_full;
mf.N_cpi = int32(rd_ctx.n_cpi);
status_cb(sprintf('[RD] 输出文件：%s', out_file));

channel_ids = parse_bundle.channel_ids;
if isempty(channel_ids)
    channel_ids = parse_bundle.rx_param.channels;
end
channel_files = parse_bundle.rx_channel_files;
channel_var_names = parse_bundle.channel_var_names;
if isempty(channel_var_names)
    channel_var_names = arrayfun(@(channel_id) sprintf('rx_ch%d', channel_id), channel_ids, 'UniformOutput', false);
end
if numel(channel_files) ~= numel(channel_ids)
    error('process_rd:InvalidChannelFiles', '通道文件数量与通道编号数量不一致。');
end

output_names = cell(1, numel(channel_ids));
for i = 1:numel(channel_ids)
    output_names{i} = sprintf('RD_Ch%d_All', channel_ids(i));
end
if numel(output_names) >= 3
    output_names{1} = 'RD_Sum_All';
    output_names{2} = 'RD_Az_All';
    output_names{3} = 'RD_El_All';
end

shared_preproc_state = preproc_state;
% 第一通道负责估计全局对齐量，其他通道直接复用该结果。
for channel_idx = 1:numel(channel_ids)
    channel_id = channel_ids(channel_idx);
    channel_file = channel_files{channel_idx};
    channel_var_name = channel_var_names{channel_idx};
    if ~exist(channel_file, 'file')
        error('process_rd:MissingChannelFile', '缺少通道数据文件：%s', channel_file);
    end

    channel_preproc_state = reset_channel_state(shared_preproc_state);
    estimate_alignment = (channel_idx == 1);
    status_cb(sprintf('[RD] 开始处理通道 ch%d：%s', channel_id, channel_file));

    mf_rx = matfile(channel_file);
    [n_blocks_out, channel_preproc_state] = process_one_channel( ...
        mf_rx, channel_var_name, rd_ctx, process_cfg, channel_preproc_state, ...
        estimate_alignment, mf, output_names{channel_idx}, status_cb);

    if channel_idx == 1
        shared_preproc_state.global_delta_p = channel_preproc_state.global_delta_p;
        shared_preproc_state.freq_offsets = channel_preproc_state.freq_offsets;
        shared_preproc_state.phase_starts = channel_preproc_state.phase_starts;
    end

    if channel_idx == numel(channel_ids)
        mf.total_blocks = int32(n_blocks_out);
    end

    status_cb(sprintf('[RD] 通道 ch%d 处理完成，输出块数=%d', channel_id, n_blocks_out));
end

clear mf;
status_cb(sprintf('[RD] 全部通道处理完成：%s', out_file));
end

function [n_blocks_written, preproc_state] = process_one_channel(mf_rx, channel_var_name, rd_ctx, process_cfg, preproc_state, estimate_alignment, mf_out, output_name, status_cb)
%PROCESS_ONE_CHANNEL 单通道 RD 处理。
%
% 输入：
%   mf_rx              - 原始通道 matfile
%   channel_var_name   - 原始通道变量名
%   rd_ctx             - RD 上下文
%   process_cfg        - RD 处理配置结构体
%   preproc_state      - 当前通道预处理状态
%   estimate_alignment - 是否允许当前通道估计对齐量
%   mf_out             - 输出 matfile
%   output_name        - 输出变量名
%   status_cb          - 状态输出函数
% 输出：
%   n_blocks_written   - 当前通道写入的 RD 块数
%   preproc_state      - 更新后的通道预处理状态
% 作用：
%   逐 chunk 读取原始通道，调用 preprocess 处理，再把块级 RD 结果写入 mat 文件。
%   单独保留这个子函数的原因是：通道级处理和总流程调度分开后，
%   更容易检查单通道问题，也便于后续扩展更多通道组合方式。

residual_buffer = [];
block_global_idx = 1;
last_phase = 0;
effective_frames = rd_ctx.total_frames_global - 1;

if process_cfg.process.do_mti_twopulse
    status_cb(sprintf('[RD] MTI 参数：两脉冲相消=%d', ...
        process_cfg.process.do_mti_twopulse));
end
if process_cfg.preprocess.do_dw_blank
    status_cb(sprintf('[预处理] 直达波空白区间：[%d, %d]，共 %d 个采样点', ...
        preproc_state.dw_lo, preproc_state.dw_hi, preproc_state.dw_hi - preproc_state.dw_lo + 1));
end

for chunk_idx = 1:rd_ctx.num_chunks
    idx_s = (chunk_idx - 1) * rd_ctx.frames_per_chunk + 1;
    idx_e = min(chunk_idx * rd_ctx.frames_per_chunk, effective_frames);
    N_cur = idx_e - idx_s + 1;
    if N_cur <= 0
        continue;
    end

    samp_s = preproc_state.dw_offset + (idx_s - 1) * rd_ctx.pri_len + 1;
    samp_e = preproc_state.dw_offset + idx_e * rd_ctx.pri_len;
    cur = reshape(single(mf_rx.(channel_var_name)(samp_s:samp_e, 1)), rd_ctx.pri_len, N_cur);

    [PC_corr, preproc_state] = preprocess( ...
        'chunk', cur, rd_ctx, process_cfg.preprocess, preproc_state, ...
        estimate_alignment, chunk_idx, last_phase, status_cb);
    last_phase = preproc_state.last_phase;

    if ~isempty(residual_buffer)
        PC_corr = [residual_buffer, PC_corr]; %#ok<AGROW>
    end
    N_comb = size(PC_corr, 2);
    n_blk = floor((N_comb - rd_ctx.n_cpi) / rd_ctx.n_step) + 1;
    if n_blk <= 0
        residual_buffer = PC_corr;
        clear PC_corr;
        continue;
    end

    blk_start_k = block_global_idx;
    chunk_sub = zeros(rd_ctx.max_calc_samples, rd_ctx.n_cpi, n_blk, 'single');
    n_done = 0;

    for blk_idx = 1:n_blk
        sf = (blk_idx - 1) * rd_ctx.n_step + 1;
        ef = sf + rd_ctx.n_cpi - 1;
        if ef > N_comb || block_global_idx > rd_ctx.total_blocks
            break;
        end

        blk = PC_corr(:, sf:ef);
        if process_cfg.process.do_mti_twopulse
            proc_blk = blk(:, 2:end) - blk(:, 1:end - 1);
        else
            proc_blk = blk;
        end
        n_v = size(proc_blk, 2);
        chunk_sub(:, :, n_done + 1) = fftshift(fft(proc_blk .* single(hamming(n_v).'), rd_ctx.n_cpi, 2), 2);

        block_global_idx = block_global_idx + 1;
        n_done = n_done + 1;
    end

    if n_done > 0
        mf_out.(output_name)(1:rd_ctx.max_calc_samples, 1:rd_ctx.n_cpi, ...
            blk_start_k:blk_start_k + n_done - 1) = chunk_sub(:, :, 1:n_done);
    end
    clear chunk_sub;

    if n_done == 0
        residual_buffer = PC_corr;
    else
        ns = n_done * rd_ctx.n_step + 1;
        if ns <= N_comb
            residual_buffer = PC_corr(:, ns:end);
        else
            residual_buffer = [];
        end
    end
    clear PC_corr;

    if mod(chunk_idx, 40) == 0 || chunk_idx == rd_ctx.num_chunks
        status_cb(sprintf('[RD] 已完成 chunk %d/%d，当前累计块数=%d', ...
            chunk_idx, rd_ctx.num_chunks, block_global_idx - 1));
    end
end

n_blocks_written = block_global_idx - 1;
end

function preproc_state = reset_channel_state(preproc_state)
%RESET_CHANNEL_STATE 重置单通道状态，同时保留跨通道共享对齐结果。
%
% 输入：preproc_state - 预处理初始化状态
% 输出：preproc_state - 当前通道使用的预处理状态
% 作用：每个通道都从同一组预处理参数开始，但相位和块状态需要单通道独立。
%   单独保留这个小函数的原因是：跨通道共享和单通道重置这两个动作容易混淆，
%   集中写清楚后更不容易在后续修改时破坏状态传递逻辑。

preproc_state.phase_starts = zeros(size(preproc_state.phase_starts), 'like', preproc_state.phase_starts);
preproc_state.last_phase = 0;
end
