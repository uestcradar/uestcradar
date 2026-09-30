function align_result = align_direct_wave_range(raw_spec, tx, pri_len, preprocess_cfg, status_cb)
%ALIGN_DIRECT_WAVE_RANGE 直达波定位与距离零点校准。
%
% 输入：
%   raw_spec       - 原始输入路径集合；需含 parsed_ch1_file/parsed_ch1_var/initial_scan_pri
%   tx             - TX 参考波形结构体
%   pri_len        - 单个 PRI 的采样点数
%   preprocess_cfg - 预处理配置，使用 do_dw_calibrate 和 dw_bin_manual
%   status_cb      - 状态输出函数
% 输出：
%   align_result   - 直达波对齐结果结构体，包含 dw_bin、range_zero_bin 和 mode
% 作用：
%   根据入口配置决定使用手动直达波 bin 还是自动标定结果。
%   从已解析 flat .mat 读取干净数据，跨波位相干平均自动定位直达波。

if nargin < 5 || isempty(status_cb)
    status_cb = @(msg) fprintf('%s\n', msg);
end

align_result = struct();
if ~preprocess_cfg.do_dw_calibrate
    align_result.range_zero_bin = preprocess_cfg.dw_bin_manual - 1;
    align_result.dw_bin = preprocess_cfg.dw_bin_manual;
    align_result.mode = 'manual';
    status_cb(sprintf('[直达波] 使用手动 bin：%d', align_result.dw_bin));
    return;
end

% 频域匹配滤波参考（与 preprocess.m 的 rd_ctx.conj_ref_freq 一致）
ref_freq = fft(single(tx.data), pri_len);
conj_ref_freq = conj(ref_freq) .* single(hamming(pri_len));

% ---- 选择标定数据源：已解析 flat .mat ----
if isfield(raw_spec, 'parsed_ch1_file') && ~isempty(raw_spec.parsed_ch1_file) ...
        && isfield(raw_spec, 'parsed_ch1_var') && ~isempty(raw_spec.parsed_ch1_var)
    % 新帧格式：从 flat 通道 mat 文件读取（变量在顶层，无嵌套）
    status_cb('[直达波] 使用已解析通道数据进行标定');
    cal_offset_pri = 0;
    if isfield(raw_spec, 'initial_scan_pri') && ~isempty(raw_spec.initial_scan_pri)
        cal_offset_pri = raw_spec.initial_scan_pri;
    end
    range_zero_bin = local_calibrate_range_zero_from_parsed( ...
        raw_spec.parsed_ch1_file, raw_spec.parsed_ch1_var, pri_len, ...
        256, conj_ref_freq, cal_offset_pri);
else
    error('align_direct_wave_range:NoCalibrationSource', ...
        '未找到可用于直达波标定的数据源（需 parsed_ch1_file）。');
end

align_result.range_zero_bin = range_zero_bin;
align_result.dw_bin = range_zero_bin + 1;
align_result.mode = 'auto';
status_cb(sprintf('[直达波] 自动标定完成：rangeZeroBin=%d，dw_bin=%d', ...
    range_zero_bin, align_result.dw_bin));
end

% =========================================================================
%  新格式标定：从 flat .mat 读取（变量在顶层，无嵌套）
% =========================================================================

function range_zero_bin = local_calibrate_range_zero_from_parsed(mat_file, var_name, pri_len, ...
    coherent_pri, conj_ref_freq, cal_offset_pri)
%LOCAL_CALIBRATE_RANGE_ZERO_FROM_PARSED 从 flat 通道 mat 文件估计距离零点。
% 拆分后变量（ch0/ch1/ch2）在 .mat 顶层，直接用 matfile 访问。
%
% 直达波是数字域漏泄（TX 数字 -> RX 数字内部通路），位置由硬件流水延迟决定，
% 每次上电不同（约 202~226 样本）。它在每个波位/每个 PRI 里完全相同（内部通路，
% 与波位指向无关），而目标回波随波位指向变化。因此关键不是“找最强峰”（最强峰
% 可能是某个波位指向上的回波），而是“跨波位相干平均”：直达波相位稳定、相干累加
% 顶出；回波相位随波位漂移、平均相消。再在盲区窗口 [dw_search_lo, dw_search_hi]
% 内取峰，位置即 range_zero_bin。

dw_search_lo = 100;   % 盲区窗口下界（0 基 bin，硬件流水延迟 ~200 样本 ± 裕量）
dw_search_hi = 250;   % 盲区窗口上界（0 基 bin）

n_dwell = 40;                       % 采样驻留数（跨整段采集，覆盖多个波位）
dwell_pri = coherent_pri;           % 每个驻留的 PRI 数（= CPI 脉冲数）

% 通道数据总 PRI 数（从变量尺寸反推，用于把驻留均匀撒到整段采集）
w = whos('-file', mat_file);
w = w(strcmp({w.name}, var_name));
if isempty(w)
    error('align_direct_wave_range:MissingVar', 'mat 文件 %s 中未找到变量 %s', mat_file, var_name);
end
total_pri = floor(double(w.size(1)) / pri_len);

mf = matfile(mat_file);

% 均匀采样 n_dwell 个驻留，跨多个波位；逐驻留频域匹配滤波 + 驻留内相干平均，
% 再跨驻留相干累加（复数），最后取模。直达波被顶出，波位相关回波被压制。
% 从 initial_scan_pri（首个完整扫描起点）往后撒驻留，避开采集启动瞬态。
start_base = max(1, double(cal_offset_pri));
span = total_pri - dwell_pri - 1 - start_base;
if span < 0, span = 0; end
prof_coh = zeros(pri_len, 1);
for di = 1:n_dwell
    start_pri = round(start_base + (di - 1) * span / max(n_dwell - 1, 1));
    samp0 = (start_pri - 1) * pri_len + 1;
    rx = reshape(double(mf.(var_name)(samp0 : samp0 + dwell_pri * pri_len - 1, 1)), ...
        pri_len, dwell_pri);

    rx = rx - mean(rx, 1);   % 快时间去均值（与 preprocess 的 DC 去除一致）
    % 频域匹配滤波（与 preprocess.m 的 rd_ctx.conj_ref_freq 一致）
    pc = ifft(bsxfun(@times, fft(rx, pri_len, 1), conj_ref_freq), pri_len, 1);
    prof_coh = prof_coh + mean(pc, 2);   % 驻留内相干平均，跨驻留相干累加（复数）
end
profile = abs(prof_coh).';

% 在盲区窗口内找直达波峰（1 基索引：bin 100~250 => 列 101~251，见上方 dw_search_lo/hi）
seg = profile(dw_search_lo + 1 : dw_search_hi + 1);
[~, local_idx] = max(seg);
range_zero_bin = dw_search_lo + local_idx - 1;   % 0 基 bin
end
