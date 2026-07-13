function [PC_corr, preproc_state] = preprocess(mode, varargin)
%PREPROCESS 预处理模块统一入口。
%
% 输入：
%   mode      - 调用模式，支持 'init' 和 'chunk'
%   varargin  - 不同模式对应的参数列表
% 输出：
%   PC_corr       - 预处理后的距离压缩数据；在 'init' 模式下为空
%   preproc_state - 预处理状态，供后续 chunk 复用
% 作用：
%   把预处理拆成两种模式统一管理：
%   1. init  只负责初始化预处理状态，例如直达波定位与缓存状态准备
%   2. chunk 负责对当前数据块真正执行预处理运算
%   这样主流程只初始化一次，后续在 RD 循环中按 chunk 重复调用。
%   单独保留这个模块的原因是：预处理既包含“一次性初始化”又包含“逐块运算”，
%   单独收口后，主流程可以保持顺序清晰，后续也更方便替换预处理链路。

switch lower(mode)
    case 'init'
        [raw_spec, tx, rd_ctx, preprocess_cfg, status_cb] = deal(varargin{:});
        preproc_state = preprocess_init_state(raw_spec, tx, rd_ctx, preprocess_cfg, status_cb);
        PC_corr = [];

    case 'chunk'
        [cur, rd_ctx, preprocess_cfg, preproc_state, estimate_alignment, chunk_idx, last_phase, status_cb] = deal(varargin{:});
        [PC_corr, preproc_state] = preprocess_chunk( ...
            cur, rd_ctx, preprocess_cfg, preproc_state, estimate_alignment, chunk_idx, last_phase, status_cb);

    otherwise
        error('preprocess:InvalidMode', '不支持的预处理调用模式：%s', mode);
end
end

function preproc_state = preprocess_init_state(raw_spec, tx, rd_ctx, preprocess_cfg, status_cb)
%PREPROCESS_INIT_STATE 初始化预处理状态。
%
% 输入：
%   data_dir       - 数据目录
%   tx             - TX 参考波形结构体
%   rd_ctx         - RD 上下文
%   preprocess_cfg - 预处理配置
%   status_cb      - 状态输出函数
% 输出：
%   preproc_state  - 预处理状态结构体
% 作用：
%   完成直达波对齐，并准备后续 chunk 处理需要的缓存状态。
%   单独保留这一步的原因是：这些量只需要算一次，
%   不应在每个 chunk 上重复计算，否则会增加开销且容易引入不一致。

align_result = align_direct_wave_range(raw_spec, tx, rd_ctx.pri_len, preprocess_cfg, status_cb);

preproc_state = struct();
preproc_state.dw_bin = align_result.dw_bin;
preproc_state.dw_offset = align_result.dw_bin - 1;
preproc_state.dw_lo = 1;
preproc_state.dw_hi = min(rd_ctx.pri_len, rd_ctx.pw_samples + preprocess_cfg.n_guard);
preproc_state.global_delta_p = 0;
preproc_state.freq_offsets = zeros(1, rd_ctx.num_chunks);
preproc_state.phase_starts = zeros(1, rd_ctx.num_chunks);
preproc_state.last_phase = 0;
preproc_state.dw_mode = align_result.mode;
preproc_state.range_zero_bin = align_result.range_zero_bin;
preproc_state.align_ready = false;
end

function [PC_corr, preproc_state] = preprocess_chunk(cur, rd_ctx, preprocess_cfg, preproc_state, estimate_alignment, chunk_idx, last_phase, status_cb)
%PREPROCESS_CHUNK 对单个 chunk 执行预处理。
%
% 输入：
%   cur                - 当前 chunk 的原始回波
%   rd_ctx             - RD 上下文
%   preprocess_cfg     - 预处理配置
%   preproc_state      - 预处理状态
%   estimate_alignment - 是否允许估计对齐量
%   chunk_idx          - 当前 chunk 编号
%   last_phase         - 前一 chunk 结束时的累计相位
%   status_cb          - 状态输出函数
% 输出：
%   PC_corr            - 预处理后的 chunk
%   preproc_state      - 更新后的预处理状态
% 作用：
%   依次完成直达波空白、去均值、距离压缩、子采样对齐和频率对齐。
%   单独保留这一步的原因是：真正的大数据处理必须按块执行，
%   把 chunk 级预处理集中在这里，更便于控制内存、检查中间结果和后续扩展步骤。

if nargin < 8 || isempty(status_cb)
    status_cb = @(msg) fprintf('%s\n', msg);
end

N_cur = size(cur, 2);

if preprocess_cfg.do_dw_blank
    cur(preproc_state.dw_lo:preproc_state.dw_hi, :) = 0;
end

if preprocess_cfg.do_fast_dc_remove
    cur = cur - mean(cur, 1);
end

PC_full = ifft(bsxfun(@times, fft(cur, rd_ctx.pri_len, 1), rd_ctx.conj_ref_freq), rd_ctx.pri_len, 1);
clear cur;

if preprocess_cfg.do_subsample_align
    if estimate_alignment && ~preproc_state.align_ready
        n_pre = min(512, N_cur);
        PC_pre = PC_full(:, 1:n_pre);
        PC_pre = PC_pre - mean(PC_pre, 1);
        energy_pre = mean(abs(PC_pre).^2, 2);
        y1_p = energy_pre(rd_ctx.pri_len);
        y2_p = energy_pre(1);
        y3_p = energy_pre(2);
        den = 2 * y2_p - y1_p - y3_p;
        if den > 1e-6
            preproc_state.global_delta_p = 0.5 * (y1_p - y3_p) / den;
        else
            preproc_state.global_delta_p = 0;
        end
        preproc_state.align_ready = true;
        status_cb(sprintf('[预处理] 对齐偏移=%d 点，子采样修正量=%.4f', ...
            preproc_state.dw_offset, preproc_state.global_delta_p));
        clear PC_pre energy_pre;
    end

    if abs(preproc_state.global_delta_p) > 1e-4
        k_ax = single((0:rd_ctx.pri_len - 1).');
        k_ax(k_ax > rd_ctx.pri_len / 2) = k_ax(k_ax > rd_ctx.pri_len / 2) - rd_ctx.pri_len;
        sub_op = exp(-1j * 2 * pi * preproc_state.global_delta_p * k_ax / rd_ctx.pri_len);
        PC_full = ifft(bsxfun(@times, fft(PC_full, rd_ctx.pri_len, 1), sub_op), rd_ctx.pri_len, 1);
    end
end

PC = PC_full(1:rd_ctx.max_calc_samples, :);
clear PC_full;

if preprocess_cfg.do_freq_comp
    if estimate_alignment && preproc_state.freq_offsets(chunk_idx) == 0
        [~, il] = max(mean(abs(PC), 2));
        bins = il:min(il + 2, rd_ctx.max_calc_samples);
        ph_time = unwrap(angle(PC(bins, :)), [], 2);
        ph_mean = mean(ph_time, 1);
        pd = diff(ph_mean);
        med_d = median(pd);
        for oi = find(abs(pd - med_d) > pi)
            ph_mean(oi + 1:end) = ph_mean(oi + 1:end) - ...
                round((ph_mean(oi + 1) - ph_mean(oi)) / (2 * pi)) * 2 * pi;
        end
        t_s = (0:N_cur - 1) * rd_ctx.prt;
        cf = polyfit(t_s, ph_mean, 1);
        fo = cf(1) / (2 * pi);
        fo = sign(fo) * min(abs(fo), 500);
        preproc_state.freq_offsets(chunk_idx) = fo;
    else
        fo = preproc_state.freq_offsets(chunk_idx);
    end

    t_fast = single((0:rd_ctx.max_calc_samples - 1).' / rd_ctx.fs);
    t_slow = single((0:N_cur - 1) * rd_ctx.prt);
    ph_mat = bsxfun(@plus, 2 * pi * fo * t_fast, 2 * pi * fo * t_slow + last_phase);
    PC_corr = PC .* exp(-1j * ph_mat);
    preproc_state.phase_starts(chunk_idx) = last_phase;
    preproc_state.last_phase = last_phase + 2 * pi * fo * (N_cur * rd_ctx.prt);
    clear ph_mat;
else
    PC_corr = PC;
    preproc_state.phase_starts(chunk_idx) = last_phase;
    preproc_state.last_phase = last_phase;
end
clear PC;
end
