function align_result = align_direct_wave_range(raw_spec, tx, pri_len, preprocess_cfg, status_cb)
%ALIGN_DIRECT_WAVE_RANGE 直达波定位与距离零点校准。
%
% 输入：
%   raw_spec       - 原始输入路径集合；新格式需含 parsed_ch1_file/parsed_ch1_var；
%                    旧格式需含 rx_files, rx_meta_file；兼容 data_dir
%   tx             - TX 参考波形结构体
%   pri_len        - 单个 PRI 的采样点数
%   preprocess_cfg - 预处理配置，使用 do_dw_calibrate 和 dw_bin_manual
%   status_cb      - 状态输出函数
% 输出：
%   align_result   - 直达波对齐结果结构体，包含 dw_bin、range_zero_bin 和 mode
% 作用：
%   根据入口配置决定使用手动直达波 bin 还是自动标定结果。
%   新帧格式优先从已解析 rx_ch*.mat 读取干净数据；旧格式回退到原始 bin。

if nargin < 5 || isempty(status_cb)
    status_cb = @(msg) fprintf('%s\n', msg);
end

align_result = struct();
data_dir = '';
if isstruct(raw_spec) && isfield(raw_spec, 'data_dir')
    data_dir = raw_spec.data_dir;
elseif ischar(raw_spec) || isStringScalar(raw_spec)
    data_dir = char(raw_spec);
end
if ~preprocess_cfg.do_dw_calibrate
    align_result.range_zero_bin = preprocess_cfg.dw_bin_manual - 1;
    align_result.dw_bin = preprocess_cfg.dw_bin_manual;
    align_result.mode = 'manual';
    status_cb(sprintf('[直达波] 使用手动 bin：%d', align_result.dw_bin));
    return;
end

tx_abs = abs(tx.data);
active_mask = tx_abs > 0.01 * max(tx_abs);
tx_active = tx.data(active_mask);
tx_active = tx_active(:);
ref_len = numel(tx_active);
tx_ref_norm = tx_active / sqrt(sum(abs(tx_active).^2));
matched_filter = conj(flipud(tx_ref_norm));

% ---- 选择标定数据源：已解析 .mat > 原始 bin > 旧目录结构 ----
if isfield(raw_spec, 'parsed_ch1_file') && ~isempty(raw_spec.parsed_ch1_file) ...
        && isfield(raw_spec, 'parsed_ch1_var') && ~isempty(raw_spec.parsed_ch1_var)
    % 新帧格式：从已解析通道 mat 文件读取干净数据
    status_cb('[直达波] 使用已解析通道数据进行标定');
    cal_offset_pri = 0;
    if isfield(raw_spec, 'initial_scan_pri') && ~isempty(raw_spec.initial_scan_pri)
        cal_offset_pri = raw_spec.initial_scan_pri;
    end
    range_zero_bin = local_calibrate_range_zero_from_parsed( ...
        raw_spec.parsed_ch1_file, raw_spec.parsed_ch1_var, pri_len, ...
        256, matched_filter, ref_len, cal_offset_pri);

elseif isfield(raw_spec, 'rx_files') && ~isempty(raw_spec.rx_files)
    % 旧格式兼容：直接读原始 bin 文件（CS16 交错）
    first_cpi = raw_spec.rx_files{1};
    rx_meta = [];
    if isfield(raw_spec, 'rx_meta_file') && ~isempty(raw_spec.rx_meta_file) ...
            && exist(raw_spec.rx_meta_file, 'file')
        rx_meta = jsondecode(fileread(raw_spec.rx_meta_file));
    end
    if isempty(rx_meta)
        num_rx_channels = 3;
    else
        num_rx_channels = numel(double(rx_meta.channels));
    end
    status_cb('[直达波] 使用原始 bin 文件进行标定');
    range_zero_bin = local_calibrate_range_zero(first_cpi, pri_len, 256, ...
        num_rx_channels, 1, matched_filter, ref_len);

else
    error('align_direct_wave_range:NoCalibrationSource', ...
        '未找到可用于直达波标定的数据源（需 parsed_ch1_file 或 rx_files）。');
end

align_result.range_zero_bin = range_zero_bin;
align_result.dw_bin = range_zero_bin + 1;
align_result.mode = 'auto';
status_cb(sprintf('[直达波] 自动标定完成：rangeZeroBin=%d，dw_bin=%d', ...
    range_zero_bin, align_result.dw_bin));
end

% =========================================================================
%  旧格式标定：从原始 CS16 bin 文件读取
% =========================================================================

function range_zero_bin = local_calibrate_range_zero(file_path, pri_len, coherent_pri, ...
    num_rx_channels, selected_channel_pos, matched_filter, ref_len)
%LOCAL_CALIBRATE_RANGE_ZERO 从旧 CS16 bin 文件估计距离零点。

cal_pri_num = min(64, coherent_pri);
rx_cal_mat_raw = local_read_cpri(file_path, pri_len, 1, 2 * cal_pri_num, num_rx_channels, selected_channel_pos);

mean_raw = mean(abs(rx_cal_mat_raw), 1);
energy_sum = conv(mean_raw, ones(1, 128), 'valid');
[~, min_offset] = min(energy_sum(1:min(pri_len, numel(energy_sum))));

rx_win = zeros(cal_pri_num, pri_len);
for pri_idx = 1:cal_pri_num
    dp = [rx_cal_mat_raw(2 * pri_idx - 1, :), rx_cal_mat_raw(2 * pri_idx, :)];
    rx_win(pri_idx, :) = dp(min_offset:min_offset + pri_len - 1);
end

rx_win = rx_win - mean(rx_win, 2);
pc_cal = conv2(rx_win, matched_filter.', 'full');
profile = mean(abs(pc_cal), 1);  % 非相干平均，避免相位漂移抵消直达波

thr = 5 * median(profile);
peak_idx = [];
for i = 2:numel(profile) - 1
    if profile(i) > thr && profile(i) >= profile(i - 1) && profile(i) >= profile(i + 1)
        peak_idx = i;
        break;
    end
end
if isempty(peak_idx)
    [~, peak_idx] = max(profile);
end

range_zero_bin = mod(min_offset + peak_idx - ref_len - 1, pri_len);
end

function rx_mat = local_read_cpri(file_path, pri_len, start_pri, num_pri, num_rx_channels, channel_pos)
%LOCAL_READ_CPRI 从旧 CS16 bin 文件读取连续 PRI 块。

bytes_per_pri = pri_len * num_rx_channels * 4;
fid = fopen(file_path, 'rb');
if fid == -1
    error('align_direct_wave_range:OpenFailed', '无法打开文件：%s', file_path);
end
fseek(fid, (start_pri - 1) * bytes_per_pri, 'bof');
raw = fread(fid, num_pri * pri_len * num_rx_channels * 2, 'int16=>single', 0, 'ieee-le');
fclose(fid);

raw_mat = reshape(raw, 2 * num_rx_channels, []);
i_row = (channel_pos - 1) * 2 + 1;
rx = complex(raw_mat(i_row, :), raw_mat(i_row + 1, :));
rx_mat = reshape(rx, pri_len, num_pri).';
end

% =========================================================================
%  新格式标定：从已解析 rx_ch*.mat 读取
% =========================================================================

function range_zero_bin = local_calibrate_range_zero_from_parsed(mat_file, var_name, pri_len, ...
    coherent_pri, matched_filter, ref_len, cal_offset_pri)
%LOCAL_CALIBRATE_RANGE_ZERO_FROM_PARSED 从已解析通道 mat 文件估计距离零点。
%
% 与旧格式的关键区别：mat 文件中每行已是完整 PRI（无需两两配对），
% 故取前 64 行做匹配滤波时，用后半区（65:128）行拼接来处理 min_offset 窗口环绕。

cal_pri_num = min(64, coherent_pri);
total_rows = 2 * cal_pri_num;  % 128 行：前半区滤波 + 后半区拼接环绕

% 从有效扫描起始位置读取 PRI 数据（而非文件开头）
mf = matfile(mat_file);
total_samples = total_rows * pri_len;
samp0 = double(cal_offset_pri) * pri_len + 1;
rx_data = mf.(var_name)(samp0 : samp0 + total_samples - 1, 1);
rx_cal_mat_raw = reshape(double(rx_data), pri_len, total_rows).';  % 128 × 1024

% 能量估计（全部 128 行）
mean_raw = mean(abs(rx_cal_mat_raw), 1);
energy_sum = conv(mean_raw, ones(1, 128), 'valid');
[~, min_offset] = min(energy_sum(1:min(pri_len, numel(energy_sum))));

% 匹配滤波（相邻 PRI 两两配对，与旧格式一致）
rx_win = zeros(cal_pri_num, pri_len);
for pri_idx = 1:cal_pri_num
    dp = [rx_cal_mat_raw(2*pri_idx-1, :), rx_cal_mat_raw(2*pri_idx, :)];
    rx_win(pri_idx, :) = dp(min_offset:min_offset + pri_len - 1);
end

rx_win = rx_win - mean(rx_win, 2);
pc_cal = conv2(rx_win, matched_filter.', 'full');
profile = mean(abs(pc_cal), 1);  % 非相干平均，避免相位漂移抵消直达波

thr = 5 * median(profile);
peak_idx = [];
for i = 2:numel(profile) - 1
    if profile(i) > thr && profile(i) >= profile(i - 1) && profile(i) >= profile(i + 1)
        peak_idx = i;
        break;
    end
end
if isempty(peak_idx)
    [~, peak_idx] = max(profile);
end

range_zero_bin = mod(min_offset + peak_idx - ref_len - 1, pri_len);
end
