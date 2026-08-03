function parse_bundle = batch_parse_bin(raw_spec)
%BATCH_PARSE_BIN 解析 5000×256-bit 固定帧格式的雷达数据。
%
% 新帧格式（Word 编号从 0 开始）：
%   #880        : 波位元数据
%   #881~#883   : DATA 区前 3 个无效 Word (Data Header)
%   #884~#4979  : 4096 个有效 RF Word
%                 = 4 个 PRI × 1024 个采样/PRI
%
% RF Word 默认解释：
%   1 个 256-bit Word 对应同一快时间采样点；
%   每个 32-bit lane 为一个 CS16 复采样：低 16 bit 为 I，高 16 bit 为 Q；
%   RF 数据沿用旧格式的小端字节序。
%
% 波位元数据默认解释：
%   32-bit 字段按高字节在前（big-endian）解析；
%   pulse_in_beam = scan_meta0[20:9]；
%   az_code       = bits[143:128]；
%   el_code       = bits[175:160]；
%   角度(°)       = code × 0.05 - 50，合法编码为 0~2000。
%
% 必需输入字段：
%   raw_spec.data_dir
%   raw_spec.tx_file
%   raw_spec.tx_meta_file
%   raw_spec.rx_meta_file
%   raw_spec.rx_files
%
% 可选输入字段：
%   raw_spec.output_dir
%   raw_spec.lane_map
%       输出通道对应的 32-bit lane，默认 1:num_channels。
%       例如三个通道位于前三个 lane 时为 [1 2 3]。
%   raw_spec.pulse_index_reference
%       'first'（默认）：元数据中的脉冲号对应本帧第 1 个 PRI，
%                       后三个 PRI 的序号依次加 1。
%       'last'         ：元数据中的脉冲号对应本帧第 4 个 PRI。
%       'same'         ：四个 PRI 均保留相同的原始脉冲号。
%
% 输出：
%   parse_bundle - 与旧解析器相近的结果结构体，并新增 beam_meta。

fprintf('====== 开始解析新帧格式数据集：%s ======\n', raw_spec.data_dir);

%% 输入与路径
required_fields = {'data_dir', 'tx_file', 'tx_meta_file', 'rx_meta_file', 'rx_files'};
for k = 1:numel(required_fields)
    field_name = required_fields{k};
    if ~isfield(raw_spec, field_name)
        error('batch_parse_bin_newformat:MissingInputField', ...
            'raw_spec 缺少必需字段：%s', field_name);
    end
end

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

if ischar(rx_files) || isstring(rx_files)
    rx_files = cellstr(rx_files);
end

if ~exist(tx_file, 'file')
    error('batch_parse_bin_newformat:MissingTxFile', '未找到发射参考文件：%s', tx_file);
end
if ~exist(tx_meta_file, 'file')
    error('batch_parse_bin_newformat:MissingTxMetaFile', '未找到发射端元数据文件：%s', tx_meta_file);
end
if isempty(rx_files)
    error('batch_parse_bin_newformat:MissingRxFiles', '没有提供任何接收端原始文件。');
end
if ~exist(output_dir, 'dir')
    mkdir(output_dir);
end

%% 旧链路参数：TX 参考、PRI 长度、通道列表
fprintf('  发射参考文件：%s\n', tx_file);
[~, first_file_name, first_file_ext] = fileparts(rx_files{1});
[~, last_file_name, last_file_ext] = fileparts(rx_files{end});
fprintf('  接收文件数量：%d（%s%s ~ %s%s）\n', numel(rx_files), ...
    first_file_name, first_file_ext, last_file_name, last_file_ext);

tx_meta = read_json_local(tx_meta_file);

fs = double(tx_meta.sample_rate);
pri_len = double(tx_meta.PRI);

% RX metadata：存在则读取通道配置，否则默认 1T3R
if exist(rx_meta_file, 'file')
    rx_meta = read_json_local(rx_meta_file);
    channel_ids = double(rx_meta.channels(:).');
else
    fprintf('  [解析] RX metadata.json 不存在，默认 3 通道 (1T3R)\n');
    channel_ids = [0, 1, 2];
end
num_channels = numel(channel_ids);

if pri_len ~= 1024
    error('batch_parse_bin_newformat:UnexpectedPriLength', ...
        ['当前帧协议要求 4096 个有效 RF Word = 4 个 PRI，因此每个 PRI 应为 1024 点；' ...
         '但 tx_meta.PRI = %d。请确认元数据单位或帧协议。'], pri_len);
end
if num_channels < 1
    error('batch_parse_bin_newformat:NoChannels', 'rx_meta.channels 为空。');
end
if num_channels > 8
    error('batch_parse_bin_newformat:TooManyChannels', ...
        '一个 256-bit RF Word 最多容纳 8 个 CS16 lane，当前通道数为 %d。', num_channels);
end

if isfield(raw_spec, 'lane_map') && ~isempty(raw_spec.lane_map)
    lane_map = double(raw_spec.lane_map(:).');
else
    lane_map = 1:num_channels;
end
if numel(lane_map) ~= num_channels || any(lane_map < 1) || any(lane_map > 8) || ...
        any(mod(lane_map, 1) ~= 0) || numel(unique(lane_map)) ~= num_channels
    error('batch_parse_bin_newformat:InvalidLaneMap', ...
        'lane_map 必须包含 %d 个互不重复的整数，取值范围为 1~8。', num_channels);
end

if isfield(raw_spec, 'pulse_index_reference') && ~isempty(raw_spec.pulse_index_reference)
    pulse_index_reference = lower(char(raw_spec.pulse_index_reference));
else
    pulse_index_reference = 'first';
end
if ~ismember(pulse_index_reference, {'first', 'last', 'same'})
    error('batch_parse_bin_newformat:InvalidPulseReference', ...
        'pulse_index_reference 只能是 first、last 或 same。');
end

% TX 文件仍按旧 CS16 裸流解析。
tx_raw = read_cs16_local(tx_file, 1, 1);
if numel(tx_raw) < pri_len
    error('batch_parse_bin_newformat:ShortTxReference', ...
        'TX 参考波形只有 %d 点，少于一个 PRI 的 %d 点。', numel(tx_raw), pri_len);
end
if numel(tx_raw) > pri_len
    tx_raw = tx_raw(1:pri_len);
end

tx = struct();
tx.param = struct('sample_rate', fs, 'pri_len', pri_len, ...
    'channels', 0, 'file_name', 'lfm_tx.bin');
tx.data = single(tx_raw(:));
clear tx_raw;

%% 固定帧格式
fmt = struct();
fmt.word_bytes = 32;
fmt.words_per_frame = 5000;
fmt.frame_bytes = fmt.word_bytes * fmt.words_per_frame;  % 160000 bytes
fmt.pri_per_frame = 4;
fmt.samples_per_pri = pri_len;                            % 1024
fmt.meta_word_matlab = 881;                               % #880
fmt.valid_data_first_matlab = 885;                        % #884
fmt.valid_data_last_matlab = 4980;                        % #4979
fmt.valid_rf_words = fmt.valid_data_last_matlab - ...
    fmt.valid_data_first_matlab + 1;                      % 4096

% 帧同步校验常量（帧头128B=0x5A + DataHeader + 帧尾128B=0xA5 三重校验）
fmt.header_len      = 128;                              % 帧头 0x5A 字节数
fmt.tail_offset     = 159872;                           % 帧尾起始偏移（160000-128）
fmt.tail_len        = 128;                              % 帧尾 0xA5 字节数
fmt.tail_marker     = uint8(0xA5);                      % 帧尾填充值
fmt.dh_offset       = 28192;                            % Data Header 字节偏移（Word #881）
fmt.dh_magic        = uint8([254; 96; 96; 96]);         % 0xFE606060（小端）

expected_rf_words = fmt.pri_per_frame * fmt.samples_per_pri;
if fmt.valid_rf_words ~= expected_rf_words
    error('batch_parse_bin_newformat:InternalFormatMismatch', ...
        '有效 RF Word 数 %d 与 4×PRI 点数 %d 不一致。', ...
        fmt.valid_rf_words, expected_rf_words);
end

%% 统计完整帧数（搜索所有 0x5A 帧头，支持多段拼接）
all_segments = {};  % 每项: struct(file_idx, preamble)
for file_idx = 1:numel(rx_files)
    file_info = dir(rx_files{file_idx});
    if isempty(file_info)
        error('batch_parse_bin_newformat:MissingRxFile', ...
            'RX 文件缺失：%s', rx_files{file_idx});
    end
    file_bytes = double(file_info.bytes);

    % 全文件逐帧扫描：对每个 160KB 边界验证帧头，断裂处搜下一段
    fid = fopen(rx_files{file_idx}, 'rb');

    % 第一步：找第一个 0x5A 帧头
    fseek(fid, 0, 'bof');
    head_chunk = fread(fid, min(5e6, file_bytes), 'uint8=>uint8');
    preamble = 0;
    for bi = 1:(numel(head_chunk) - fmt.header_len + 1)
        if all(head_chunk(bi:bi + fmt.header_len - 1) == uint8(0x5A))
            preamble = bi - 1;
            break;
        end
    end
    if preamble == 0
        fclose(fid);
        error('batch_parse_bin_newformat:NoFrameHeader', ...
            '未在文件开头找到 128 字节全 0x5A 帧头。');
    end

    % 第二步：逐个 160KB 边界三重校验（帧头+DataHeader+帧尾），连续帧合并为段
    pos = preamble;
    seg_start = pos;
    while pos + fmt.frame_bytes <= file_bytes
        if verify_frame_boundary(fid, pos, fmt)
            pos = pos + fmt.frame_bytes;  % 帧OK，继续
        else
            % 段结束：收录 seg_start ~ pos-1 的连续帧
            if pos > seg_start
                all_segments{end+1} = struct('file_idx', file_idx, 'preamble', seg_start); %#ok<AGROW>
                seg_start = pos;  % 防止退出循环后重复加入
            end
            % 在附近搜索下一段帧头
            fseek(fid, pos + 1, 'bof');
            sch = fread(fid, min(10e6, file_bytes - pos), 'uint8=>uint8');
            found = false;
            for bi = 1:(numel(sch) - fmt.header_len + 1)
                if all(sch(bi:bi + fmt.header_len - 1) == uint8(0x5A))
                    pos = pos + 1 + bi - 1;
                    seg_start = pos;
                    found = true;
                    break;
                end
            end
            if ~found, break; end
        end
    end
    if pos > seg_start
        all_segments{end+1} = struct('file_idx', file_idx, 'preamble', seg_start); %#ok<AGROW>
    end
    fclose(fid);
end

if isempty(all_segments)
    error('batch_parse_bin_newformat:NoFrameHeader', '未在文件中找到 0x5A 帧头。');
end

% 每段统计帧数
frames_per_segment = zeros(1, numel(all_segments));
for s = 1:numel(all_segments)
    seg = all_segments{s};
    file_bytes = double(dir(rx_files{seg.file_idx}).bytes);
    if s < numel(all_segments)
        next_preamble = all_segments{s+1}.preamble;
    else
        next_preamble = file_bytes;
    end
    frames_per_segment(s) = floor((next_preamble - seg.preamble) / fmt.frame_bytes);
end

total_frames = sum(frames_per_segment);
fprintf('  [解析] 共找到 %d 个数据段，总计 %d 帧\n', numel(all_segments), total_frames);
total_pri = total_frames * fmt.pri_per_frame;
total_samples = total_pri * pri_len;

fprintf('  总帧数：%d（%d 段）；每帧 PRI：%d；总 PRI：%d\n', ...
    total_frames, numel(all_segments), fmt.pri_per_frame, total_pri);
fprintf('  每通道总采样点：%d；内存估计：%.2f GB（complex single）\n', ...
    total_samples, total_samples * 8 / 1e9);

if total_frames <= 0
    error('batch_parse_bin_newformat:NoCompleteFrame', ...
        '所有 RX 文件中均未找到完整的 160000-byte 帧。');
end

rf_param = struct();
if isfield(tx_meta, 'waveform')
    rf_param = tx_meta.waveform;
end

%% 创建通道输出文件
ts = char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'));
rx_channel_files = cell(1, num_channels);
channel_var_names = cell(1, num_channels);
channel_mats = cell(1, num_channels);

for channel_idx = 1:num_channels
    channel_id = channel_ids(channel_idx);
    channel_var_name = sprintf('rx_ch%d', channel_id);
    out_file = fullfile(output_dir, sprintf('%s_%s.mat', channel_var_name, ts));

    rx_channel_files{channel_idx} = out_file;
    channel_var_names{channel_idx} = channel_var_name;
    channel_mats{channel_idx} = matfile(out_file, 'Writable', true);
    channel_mats{channel_idx}.(channel_var_name)(total_samples, 1) = ...
        complex(single(0), single(0));

    fprintf('  [通道 ch%d <- lane %d] 已预分配：%s\n', ...
        channel_id, lane_map(channel_idx), out_file);
end

%% 预分配每个 PRI 的波位元数据
beam_meta = struct();
beam_meta.frame_index = zeros(total_pri, 1, 'uint64');
beam_meta.file_index = zeros(total_pri, 1, 'uint32');
beam_meta.frame_in_file = zeros(total_pri, 1, 'uint32');
beam_meta.pri_in_frame = zeros(total_pri, 1, 'uint8');       % 0~3
beam_meta.sweep_count = zeros(total_pri, 1, 'uint8');
beam_meta.beam_idx = zeros(total_pri, 1, 'uint16');
beam_meta.pulse_in_beam_raw = zeros(total_pri, 1, 'uint16');
beam_meta.pulse_in_beam = zeros(total_pri, 1, 'uint16');
beam_meta.az_code = zeros(total_pri, 1, 'uint16');
beam_meta.el_code = zeros(total_pri, 1, 'uint16');
beam_meta.az_deg = nan(total_pri, 1, 'single');
beam_meta.el_deg = nan(total_pri, 1, 'single');
beam_meta.meta_valid = false(total_pri, 1);

%% 逐段逐帧解析
sample_offset = 0;
pri_offset = 0;
global_frame_idx = 0;
bad_meta_count = 0;
meta_layout_used = '';
last_file_idx = 0;
fid = -1;

for seg_idx = 1:numel(all_segments)
    file_idx = all_segments{seg_idx}.file_idx;
    preamble = all_segments{seg_idx}.preamble;
    n_frames = frames_per_segment(seg_idx);

    % 仅在切换到新文件时打开
    if file_idx ~= last_file_idx
        if fid > 0, fclose(fid); end
        fid = fopen(rx_files{file_idx}, 'rb');
        if fid == -1
            error('batch_parse_bin_newformat:OpenFailed', ...
                '无法打开原始文件：%s', rx_files{file_idx});
        end
        last_file_idx = file_idx;
    end

    fseek(fid, preamble, 'bof');

    for frame_in_seg = 1:n_frames
        frame_bytes = fread(fid, [fmt.word_bytes, fmt.words_per_frame], ...
            'uint8=>uint8');
        if size(frame_bytes, 1) ~= fmt.word_bytes || ...
                size(frame_bytes, 2) ~= fmt.words_per_frame
            break;  % 段末尾残帧，跳过
        end

        global_frame_idx = global_frame_idx + 1;

        meta_word = frame_bytes(:, fmt.meta_word_matlab);
        [frame_meta, meta_ok, meta_layout] = parse_beam_meta_local(meta_word);
        if isempty(meta_layout_used)
            meta_layout_used = meta_layout;
        end
        if ~meta_ok
            bad_meta_count = bad_meta_count + 1;
        end

        pri_idx = pri_offset + (1:fmt.pri_per_frame);
        pri_number_in_frame = 0:(fmt.pri_per_frame - 1);

        beam_meta.frame_index(pri_idx) = uint64(global_frame_idx);
        beam_meta.file_index(pri_idx) = uint32(file_idx);
        beam_meta.frame_in_file(pri_idx) = uint32(frame_in_seg);
        beam_meta.pri_in_frame(pri_idx) = uint8(pri_number_in_frame);
        beam_meta.sweep_count(pri_idx) = frame_meta.sweep_count;
        beam_meta.beam_idx(pri_idx) = frame_meta.beam_idx;
        beam_meta.pulse_in_beam_raw(pri_idx) = frame_meta.pulse_in_beam;
        beam_meta.pulse_in_beam(pri_idx) = expand_pulse_index_local( ...
            frame_meta.pulse_in_beam, fmt.pri_per_frame, pulse_index_reference);
        beam_meta.az_code(pri_idx) = frame_meta.az_code;
        beam_meta.el_code(pri_idx) = frame_meta.el_code;
        beam_meta.az_deg(pri_idx) = frame_meta.az_deg;
        beam_meta.el_deg(pri_idx) = frame_meta.el_deg;
        beam_meta.meta_valid(pri_idx) = meta_ok;

        % DATA：跳过 #880~#883，保留 #884~#4979，共 4096 Word。
        rf_word_bytes = frame_bytes(:, ...
            fmt.valid_data_first_matlab:fmt.valid_data_last_matlab);

        % 每两个字节按小端组成一个 int16；每个 Word 共 16 个 int16，
        % 对应 8 个 CS16 lane：I1,Q1,I2,Q2,...,I8,Q8。
        rf_u16 = uint16(rf_word_bytes(1:2:end, :)) + ...
            bitshift(uint16(rf_word_bytes(2:2:end, :)), 8);
        rf_i16_single = single(rf_u16);
        negative_mask = rf_u16 >= uint16(32768);
        rf_i16_single(negative_mask) = rf_i16_single(negative_mask) - single(65536);

        sample_idx = sample_offset + (1:fmt.valid_rf_words);
        for channel_idx = 1:num_channels
            lane_idx = lane_map(channel_idx);
            i_row = (lane_idx - 1) * 2 + 1;
            q_row = i_row + 1;

            channel_var_name = channel_var_names{channel_idx};
            chunk = complex(rf_i16_single(i_row, :), ...
                rf_i16_single(q_row, :)).';
            channel_mats{channel_idx}.(channel_var_name)(sample_idx, 1) = chunk;
        end

        sample_offset = sample_offset + fmt.valid_rf_words;
        pri_offset = pri_offset + fmt.pri_per_frame;

        if mod(global_frame_idx, 1000) == 0 || global_frame_idx == total_frames
            fprintf('    已完成帧 %d/%d，累计 PRI：%d\n', ...
                global_frame_idx, total_frames, pri_offset);
        end
    end
end
if fid > 0, fclose(fid); end

if sample_offset ~= total_samples || pri_offset ~= total_pri
    error('batch_parse_bin_newformat:OutputCountMismatch', ...
        '实际写入计数与预估不一致：samples=%d/%d，PRI=%d/%d。', ...
        sample_offset, total_samples, pri_offset, total_pri);
end

for channel_idx = 1:num_channels
    fprintf('  [通道 ch%d] 已保存：%s\n', ...
        channel_ids(channel_idx), rx_channel_files{channel_idx});
end

if bad_meta_count > 0
    warning('batch_parse_bin_newformat:BadBeamMagic', ...
        '%d/%d 帧未识别到 BEAM 标识；这些帧仍已解析，但 meta_valid=false。', ...
        bad_meta_count, total_frames);
end

%% 保存解析信息
rx_param = struct();
rx_param.sample_rate = fs;
rx_param.pri_len = pri_len;
rx_param.prf = fs / pri_len;
rx_param.channels = channel_ids;
rx_param.lane_map = lane_map;
rx_param.total_frames = total_frames;
rx_param.pri_per_frame = fmt.pri_per_frame;
rx_param.total_pri = total_pri;
rx_param.total_samples = total_samples;
rx_param.words_per_frame = fmt.words_per_frame;
rx_param.bytes_per_frame = fmt.frame_bytes;
rx_param.valid_rf_words_per_frame = fmt.valid_rf_words;
rx_param.frames_per_segment = frames_per_segment;
rx_param.num_segments = numel(all_segments);
rx_param.pulse_index_reference = pulse_index_reference;
rx_param.meta_layout = meta_layout_used;
rx_param.angle_code_min = 0;
rx_param.angle_code_max = 2000;
rx_param.angle_step_deg = 0.05;
rx_param.angle_offset_deg = -50;
rx_param.parse_ts = ts;
rx_param.cpi_files = rx_files;

info_file = fullfile(output_dir, sprintf('parse_info_%s.mat', ts));
save(info_file, 'rx_param', 'rf_param', 'tx', 'beam_meta', '-v7.3');

parse_bundle = struct();
parse_bundle.data_dir = data_dir;
parse_bundle.parse_ts = ts;
parse_bundle.rx_param = rx_param;
parse_bundle.rf_param = rf_param;
parse_bundle.tx = tx;
parse_bundle.beam_meta = beam_meta;
parse_bundle.rx_channel_files = rx_channel_files;
parse_bundle.channel_var_names = channel_var_names;
parse_bundle.parse_info_file = info_file;
parse_bundle.channel_ids = channel_ids;

fprintf('\n  波位元数据和解析索引已保存：%s\n', info_file);
fprintf('====== 新帧格式数据解析完成 ======\n\n');
end

function meta = read_json_local(file_path)
%READ_JSON_LOCAL 读取 JSON 元数据文件。
meta = jsondecode(fileread(file_path));
end

function x = read_cs16_local(file_path, num_channels, channel_pos)
%READ_CS16_LOCAL 按旧格式读取小端 CS16 参考波形。
fid = fopen(file_path, 'rb');
if fid == -1
    error('batch_parse_bin_newformat:OpenTxFailed', ...
        '无法打开 TX 文件：%s', file_path);
end
cleanup_obj = onCleanup(@() fclose(fid));
raw = fread(fid, inf, 'int16=>single', 0, 'ieee-le');

sample_width = 2 * num_channels;
complete_values = floor(numel(raw) / sample_width) * sample_width;
raw = raw(1:complete_values);
raw_mat = reshape(raw, sample_width, []);
i_row = (channel_pos - 1) * 2 + 1;
x = complex(raw_mat(i_row, :), raw_mat(i_row + 1, :)).';
clear cleanup_obj;
end

function [meta, valid, layout] = parse_beam_meta_local(word_bytes)
%PARSE_BEAM_META_LOCAL 解析 #879 Word 中需要的三个字段。
%
% Word 的 32-bit 字段内部按高字节在前。根据 BEAM 标识自动判断
% 32-bit lane 是低位字段在前还是高位字段在前。

word_bytes = uint8(word_bytes(:));
if numel(word_bytes) ~= 32
    error('batch_parse_bin_newformat:BadMetaWordSize', ...
        '波位元数据 Word 必须为 32 bytes。');
end

magic = uint8([hex2dec('4D'); hex2dec('41'); hex2dec('45'); hex2dec('42')]);  % "BEAM" 小端存储为 "MAEB"
groups = reshape(word_bytes, 4, 8);

if isequal(groups(:, 1), magic)
    normalized = groups;
    valid = true;
    layout = 'low-32bit-field-first';
elseif isequal(groups(:, 8), magic)
    normalized = groups(:, end:-1:1);
    valid = true;
    layout = 'high-32bit-field-first';
else
    % 标识不匹配时仍按协议图中的“低位字段在前”尝试解析。
    normalized = groups;
    valid = false;
    layout = 'unrecognized-assume-low-32bit-field-first';
end

b = normalized(:);
scan_meta0 = read_u32_le_local(b(5:8));

% 调试：打印前几帧的 scan_meta0
persistent dbg_n;
if isempty(dbg_n), dbg_n = 0; end
if dbg_n < 5
    dbg_n = dbg_n + 1;
    fprintf('[DEBUG scan_meta0 #%d] hex=', dbg_n);
    fprintf('%02x ', b(5:8)); fprintf(' uint32=%u ', scan_meta0);
    fprintf('sweep=%d beam=%d pulse=%d\n', ...
        bitand(bitshift(scan_meta0, -27), 31), ...
        bitand(scan_meta0, 511), ...
        bitand(bitshift(scan_meta0, -9), 4095));
end

meta = struct();
meta.sweep_count = uint8(bitand(bitshift(scan_meta0, -27), uint32(31)));
meta.beam_idx = uint16(bitand(scan_meta0, uint32(511)));
meta.pulse_in_beam = uint16(bitand(bitshift(scan_meta0, -9), uint32(4095)));
% az/el 编码位于 32-bit 字段的高 16 位，小端存储：{code_le16, 16'd0}
meta.az_code = typecast(b(17:18), 'uint16');  % bytes 17-18, little-endian
meta.el_code = typecast(b(21:22), 'uint16');  % bytes 21-22, little-endian
meta.az_deg = angle_code_to_deg_local(meta.az_code);
meta.el_deg = angle_code_to_deg_local(meta.el_code);
end

function value = read_u16_be_local(bytes)
%READ_U16_BE_LOCAL 高字节在前的 uint16。
bytes = uint16(bytes(:));
value = bitor(bitshift(bytes(1), 8), bytes(2));
end

function value = read_u32_le_local(bytes)
%READ_U32_LE_LOCAL 低字节在前的 uint32（与 magic/AZ/EL/RF 字节序一致）。
bytes = uint32(bytes(:));
value = bitor(bytes(1), ...
    bitor(bitshift(bytes(2), 8), ...
    bitor(bitshift(bytes(3), 16), ...
          bitshift(bytes(4), 24))));
end

function ok = verify_frame_boundary(fid, pos, fmt)
%VERIFY_FRAME_BOUNDARY 三重校验帧边界：帧头 0x5A + Data Header + 帧尾 0xA5。
ok = false;

% 1) 帧头 128 字节全 0x5A
fseek(fid, pos, 'bof');
hdr = fread(fid, fmt.header_len, 'uint8=>uint8');
if numel(hdr) < fmt.header_len || ~all(hdr == uint8(0x5A))
    return;
end

% 2) Data Header 标识（Word #881 起始 4 字节 = 0xFE606060 小端）
fseek(fid, pos + fmt.dh_offset, 'bof');
dh = fread(fid, 4, 'uint8=>uint8');
if numel(dh) < 4 || ~all(dh == fmt.dh_magic)
    return;
end

% 3) 帧尾 128 字节全 0xA5
fseek(fid, pos + fmt.tail_offset, 'bof');
tail = fread(fid, fmt.tail_len, 'uint8=>uint8');
if numel(tail) < fmt.tail_len || ~all(tail == fmt.tail_marker)
    return;
end

ok = true;
end

function angle_deg = angle_code_to_deg_local(code)
%ANGLE_CODE_TO_DEG_LOCAL 0~2000 映射到 -50°~50°，步进 0.05°。
if code <= uint16(2000)
    angle_deg = single(double(code) * 0.05 - 50.0);
else
    angle_deg = single(NaN);
end
end

function pulse_indices = expand_pulse_index_local(raw_index, pri_per_frame, reference)
%EXPAND_PULSE_INDEX_LOCAL 将每帧一个脉冲号扩展为每 PRI 一个脉冲号。
raw_value = double(raw_index);
switch reference
    case 'first'
        first_value = raw_value;
        pulse_values = first_value + (0:(pri_per_frame - 1));
    case 'last'
        first_value = raw_value - (pri_per_frame - 1);
        pulse_values = first_value + (0:(pri_per_frame - 1));
    case 'same'
        pulse_values = repmat(raw_value, 1, pri_per_frame);
    otherwise
        error('batch_parse_bin_newformat:InvalidPulseReferenceInternal', ...
            '未知的脉冲号参考方式：%s', reference);
end

% 字段宽度为 12 bit，因此按 0~4095 回绕。
pulse_indices = uint16(mod(pulse_values, 4096));
end
