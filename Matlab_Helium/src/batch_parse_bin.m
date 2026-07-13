function parse_bundle = batch_parse_bin(raw_spec)
%BATCH_PARSE_BIN 将原始雷达文件解析为 MATLAB 通道文件。
%
% 输入：
%   raw_spec - 原始输入路径集合，必须包含 data_dir、tx_file、tx_meta_file、
%              rx_meta_file 和 rx_files
% 输出：
%   parse_bundle - 解析结果结构体，供后续预处理和 RD 处理直接使用
% 作用：
%   完成 TX 参考波形读取、RX 通道拆分、通道 mat 文件写入，以及解析信息保存。
%   单独保留这个模块的原因是：原始 bin 解析与后续信号处理属于不同阶段，
%   解析结果一旦固定，就可以长期复用，避免每次调算法都重复做大体量二进制拆分。

fprintf('====== 开始解析数据集：%s ======\n', raw_spec.data_dir);

data_dir = raw_spec.data_dir;
if isfield(raw_spec, 'output_dir') && ~isempty(raw_spec.output_dir)
    output_dir = raw_spec.output_dir;
else
    output_dir = data_dir;
end
tx_file = raw_spec.tx_file;
tx_meta_file = raw_spec.tx_meta_file;
rx_meta_file = raw_spec.rx_meta_file;
rx_files = raw_spec.rx_files;

if ~exist(tx_file, 'file')
    error('batch_parse_bin:MissingTxFile', '未找到发射参考文件：%s', tx_file);
end
if ~exist(tx_meta_file, 'file')
    error('batch_parse_bin:MissingTxMetaFile', '未找到发射端元数据文件：%s', tx_meta_file);
end
if ~exist(rx_meta_file, 'file')
    error('batch_parse_bin:MissingRxMetaFile', '未找到接收端元数据文件：%s', rx_meta_file);
end
if isempty(rx_files)
    error('batch_parse_bin:MissingCpiFiles', '没有提供任何接收端 CPI 原始文件。');
end

if ~exist(output_dir, 'dir')
    mkdir(output_dir);
end

fprintf('  发射参考文件：%s\n', tx_file);
[~, first_file_name, first_file_ext] = fileparts(rx_files{1});
[~, last_file_name, last_file_ext] = fileparts(rx_files{end});
fprintf('  接收文件数量：%d（%s%s ~ %s%s）\n', numel(rx_files), ...
    first_file_name, first_file_ext, last_file_name, last_file_ext);

tx_meta = read_json_local(tx_meta_file);
rx_meta = read_json_local(rx_meta_file);
fs = double(tx_meta.sample_rate);
pri_len = double(tx_meta.PRI);
channel_ids = double(rx_meta.channels(:).');
num_channels = numel(channel_ids);

tx_raw = read_cs16_local(tx_file, 1, 1);
if numel(tx_raw) > pri_len
    tx_raw = tx_raw(1:pri_len);
end

tx = struct();
tx.param = struct('sample_rate', fs, 'pri_len', pri_len, 'channels', 0, 'file_name', 'lfm_tx.bin');
tx.data = single(tx_raw(:));
clear tx_raw;

bytes_per_sample = 4 * num_channels;
pri_per_file = zeros(1, numel(rx_files));
for file_idx = 1:numel(rx_files)
    file_info = dir(rx_files{file_idx});
    if isempty(file_info)
        error('batch_parse_bin:MissingRxFile', 'RX 文件缺失：%s', rx_files{file_idx});
    end
    file_bytes = double(file_info.bytes);
    samples_this_file = floor(file_bytes / bytes_per_sample);
    pri_per_file(file_idx) = floor(samples_this_file / pri_len);
end
total_pri = sum(pri_per_file);
total_samples = total_pri * pri_len;

fprintf('  单文件 PRI 数：%d，总 PRI 数：%d，总采样点数：%d\n', ...
    pri_per_file, total_pri, total_samples);
if total_samples <= 0
    error('batch_parse_bin:NoCompletePri', '所有 RX 文件中未找到完整的 PRI 段。');
end
fprintf('  单通道内存估计：%.2f GB（complex single）\n', total_samples * 8 / 1e9);

rf_param = struct();
if isfield(tx_meta, 'waveform')
    rf_param = tx_meta.waveform;
end

ts = char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'));
rx_channel_files = cell(1, num_channels);
channel_var_names = cell(1, num_channels);

for channel_idx = 1:num_channels
    channel_id = channel_ids(channel_idx);
    channel_var_name = sprintf('rx_ch%d', channel_id);
    out_file = fullfile(output_dir, sprintf('%s_%s.mat', channel_var_name, ts));
    rx_channel_files{channel_idx} = out_file;
    channel_var_names{channel_idx} = channel_var_name;
    channel_mats{channel_idx} = matfile(out_file, 'Writable', true);
    % 预分配最终大小，后续 matfile 写操作按块连续写入
    channel_mats{channel_idx}.(channel_var_name)(total_samples, 1) = complex(single(0), single(0));
    fprintf('  [通道 ch%d] 已预分配输出文件：%s\n', channel_id, out_file);
end

% 单次读文件：每个 .bin 文件只读取一次，同时拆分所有通道
offset = 0;
for file_idx = 1:numel(rx_files)
    fid = fopen(rx_files{file_idx}, 'rb');
    if fid == -1
        error('batch_parse_bin:OpenFailed', '无法打开原始文件：%s', rx_files{file_idx});
    end
    raw = fread(fid, inf, 'int16=>single', 0, 'ieee-le');
    fclose(fid);

    n_pri = pri_per_file(file_idx);
    n_total = n_pri * pri_len;
    if n_total <= 0
        continue;
    end

    raw = raw(1:n_total * num_channels * 2);
    raw_mat = reshape(raw, 2 * num_channels, []);
    clear raw;

    sample_idx = offset + (1:n_total);
    for channel_idx = 1:num_channels
        channel_var_name = channel_var_names{channel_idx};
        i_row = (channel_idx - 1) * 2 + 1;
        chunk = complex(raw_mat(i_row, :), raw_mat(i_row + 1, :)).';
        channel_mats{channel_idx}.(channel_var_name)(sample_idx, 1) = chunk;
    end

    clear raw_mat;
    offset = offset + n_total;

    if mod(file_idx, 5) == 0 || file_idx == numel(rx_files)
        fprintf('    已完成文件 %d/%d，当前累计 PRI 数：%d\n', file_idx, numel(rx_files), offset / pri_len);
    end
end

for channel_idx = 1:num_channels
    fprintf('  [通道 ch%d] 已保存：%s\n', channel_ids(channel_idx), rx_channel_files{channel_idx});
end

rx_param = struct();
rx_param.sample_rate = fs;
rx_param.pri_len = pri_len;
rx_param.prf = fs / pri_len;
rx_param.channels = channel_ids;
rx_param.total_pri = total_pri;
rx_param.total_samples = total_samples;
rx_param.parse_ts = ts;
rx_param.cpi_files = rx_files;
info_file = fullfile(output_dir, sprintf('parse_info_%s.mat', ts));
save(info_file, 'rx_param', 'rf_param', 'tx');

parse_bundle = struct();
parse_bundle.data_dir = data_dir;
parse_bundle.parse_ts = ts;
parse_bundle.rx_param = rx_param;
parse_bundle.rf_param = rf_param;
parse_bundle.tx = tx;
parse_bundle.rx_channel_files = rx_channel_files;
parse_bundle.channel_var_names = channel_var_names;
parse_bundle.parse_info_file = info_file;
parse_bundle.channel_ids = channel_ids;

fprintf('\n  解析索引已保存：%s\n', info_file);
fprintf('====== 数据解析完成 ======\n\n');
end

function meta = read_json_local(file_path)
%READ_JSON_LOCAL 读取 JSON 元数据文件。
%
% 输入：file_path - JSON 文件路径
% 输出：meta      - 解析后的结构体
% 作用：把 JSON 读取封装成一个只依赖显式路径的本地小函数。
%   单独封装的原因是：后续如果元数据格式变化，只需要集中修改这一处读取逻辑。

meta = jsondecode(fileread(file_path));
end

function x = read_cs16_local(file_path, num_channels, channel_pos)
%READ_CS16_LOCAL 读取单个 CS16 参考波形。
%
% 输入：file_path    - 二进制文件路径
%       num_channels - 文件中的通道数
%       channel_pos  - 目标通道位置
% 输出：x            - 复数波形向量
% 作用：从交织的 int16 数据中提取参考 TX 波形。
%   单独封装的原因是：参考波形读取规则与 RX 多通道拆分规则相近但不完全相同，
%   拆开后更便于核对文件格式和后续复用。

fid = fopen(file_path, 'rb');
raw = fread(fid, inf, 'int16=>single', 0, 'ieee-le');
fclose(fid);
raw_mat = reshape(raw, 2 * num_channels, []);
i_row = (channel_pos - 1) * 2 + 1;
x = complex(raw_mat(i_row, :), raw_mat(i_row + 1, :)).';
end
