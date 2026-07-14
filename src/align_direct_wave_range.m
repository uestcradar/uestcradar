function align_result = align_direct_wave_range(raw_spec, tx, pri_len, preprocess_cfg, status_cb)
%ALIGN_DIRECT_WAVE_RANGE 直达波定位与距离零点校准。
%
% 输入：
%   raw_spec       - 原始输入路径集合，需包含 rx_batch_dir、rx_files、rx_meta_file；兼容 data_dir
%   tx             - TX 参考波形结构体
%   pri_len        - 单个 PRI 的采样点数
%   preprocess_cfg - 预处理配置，使用 do_dw_calibrate 和 dw_bin_manual
%   status_cb      - 状态输出函数
% 输出：
%   align_result   - 直达波对齐结果结构体，包含 dw_bin、range_zero_bin 和 mode
% 作用：
%   根据入口配置决定使用手动直达波 bin 还是自动标定结果。
%   单独保留这个模块的原因是：直达波定位既影响后续预处理链路，
%   又经常需要独立调试和验证，把它从预处理主流程中拆开更便于排查问题。

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

% 优先使用 raw_spec 中的批次路径，避免重新解析
if isfield(raw_spec, 'rx_batch_dir') && ~isempty(raw_spec.rx_batch_dir) && ...
        isfield(raw_spec, 'rx_files') && ~isempty(raw_spec.rx_files) && ...
        isfield(raw_spec, 'rx_meta_file') && exist(raw_spec.rx_meta_file, 'file')
    first_cpi = raw_spec.rx_files{1};
    rx_meta = jsondecode(fileread(raw_spec.rx_meta_file));
else
rx_root_dir = fullfile(data_dir, 'RX');
rx_batch_entries = dir(fullfile(rx_root_dir, '*'));
rx_batch_entries = rx_batch_entries([rx_batch_entries.isdir]);
rx_batch_entries = rx_batch_entries(~ismember({rx_batch_entries.name}, {'.', '..'}));
if isempty(rx_batch_entries)
    error('align_direct_wave_range:MissingRxBatchDir', '未在目录中找到 RX 批次子目录：%s', rx_root_dir);
end
[~, rx_batch_order] = sort({rx_batch_entries.name});
rx_batch_entries = rx_batch_entries(rx_batch_order);
first_batch_dir = fullfile(rx_batch_entries(1).folder, rx_batch_entries(1).name);

cpi_list = dir(fullfile(first_batch_dir, 'cpi_*.bin'));
if isempty(cpi_list)
    error('align_direct_wave_range:MissingCpiFiles', '未在首个 RX 批次目录中找到 cpi_*.bin：%s', first_batch_dir);
end
nums = cellfun(@(name) str2double(regexp(name, '\d+', 'match', 'once')), {cpi_list.name});
[~, ord] = sort(nums);
first_cpi = fullfile(first_batch_dir, cpi_list(ord(1)).name);

rx_meta = jsondecode(fileread(fullfile(first_batch_dir, 'metadata.json')));
end
num_rx_channels = numel(double(rx_meta.channels));

status_cb('[直达波] 开始自动标定距离零点');
range_zero_bin = local_calibrate_range_zero(first_cpi, pri_len, 256, ...
    num_rx_channels, 1, matched_filter, ref_len);

align_result.range_zero_bin = range_zero_bin;
align_result.dw_bin = range_zero_bin + 1;
align_result.mode = 'auto';
status_cb(sprintf('[直达波] 自动标定完成：rangeZeroBin=%d，dw_bin=%d', ...
    range_zero_bin, align_result.dw_bin));
end

function range_zero_bin = local_calibrate_range_zero(file_path, pri_len, coherent_pri, ...
    num_rx_channels, selected_channel_pos, matched_filter, ref_len)
%LOCAL_CALIBRATE_RANGE_ZERO 估计距离零点。
%
% 输入：
%   file_path             - 首个 CPI 文件路径
%   pri_len               - PRI 采样点数
%   coherent_pri          - 用于标定的相干 PRI 数
%   num_rx_channels       - 原始文件中的通道数
%   selected_channel_pos  - 参与标定的通道位置
%   matched_filter        - TX 参考波形对应的匹配滤波器
%   ref_len               - 参考波形长度
% 输出：
%   range_zero_bin        - 距离零点所在的 bin
% 作用：
%   在首个 CPI 文件中找到直达波参考位置，为后续预处理提供基准。
%   单独保留这个子函数的原因是：自动标定的细节步骤相对独立，
%   后续如果要换标定策略，只需要替换这里而不影响上层接口。

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
profile = abs(mean(pc_cal, 1));

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

range_zero_bin = mod(min_offset + peak_idx - ref_len, pri_len);
end

function rx_mat = local_read_cpri(file_path, pri_len, start_pri, num_pri, num_rx_channels, channel_pos)
%LOCAL_READ_CPRI 读取用于直达波标定的连续 PRI 块。
%
% 输入：
%   file_path        - CPI 文件路径
%   pri_len          - 单个 PRI 的采样点数
%   start_pri        - 起始 PRI 编号
%   num_pri          - 读取的 PRI 数量
%   num_rx_channels  - 通道数
%   channel_pos      - 目标通道位置
% 输出：
%   rx_mat           - 形状为 num_pri x pri_len 的复数矩阵
% 作用：
%   直接从二进制文件中提取指定通道和指定 PRI 范围的数据。
%   单独保留这个子函数的原因是：原始文件读取规则最容易受到数据格式影响，
%   抽出来后更便于单独验证每一步读数是否正确。

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
