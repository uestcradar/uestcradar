%% 雷达原始双向信号解析工具 (parse_bin.m)
%
% 说明:
%   运行此脚本将依次打开选择框：
%     1. 选择发射参考文件 txFile (*.bin)
%     2. 手动选择一个或多个接收二进制回波文件 cpi_*.bin
%   脚本将自动调用文件末尾的本地函数 load_radar_cpi，读取发射参考信号与接收二进制回波。
%   解析时核心实验参数（采样率、PRI）以发射侧元数据为准，接收通道数据以接收侧元数据为准。
%   解析及导出完成后，工作区将仅保留需要的三个结构体变量。
%
% 留存工作区变量:
%   tx       : 发射端参考信号结构体，包含:
%              .param: 发射参数结构体 (含 sample_rate, pri_len 等)
%              .data : 维度为 [pri_len x 1] 的一维复数参考波形（列向量）
%   rx       : 接收端回波信号结构体，包含:
%              .param: 接收参数结构体 (含 sample_rate, pri_len, total_pri, total_samples 等)
%              .ch0, .ch1, .ch2 等 (以选中的实际通道号命名): 维度为 [total_samples x 1] 的各通道一维连续复数信号（列向量）
%   rf_param : 最外层的信号射频参数，源于发射元数据中的 waveform 配置 (包含 type, PW, bandwidth 等)

clear; clc; close all;

%% 1. 选择发射参考文件与接收 capture 回波 bin 文件
[txFileName, txDir] = uigetfile({'*.bin', 'Binary waveform (*.bin)'; '*.*', 'All Files'}, ...
    '选择发射参考 txFile (*.bin)');
if isequal(txFileName, 0)
    disp('已取消选择发射参考文件。');
    return;
end
txFile = fullfile(txDir, txFileName);

% 手动单选或多选具体的接收 cpi_*.bin 文件
[rxFileNames, rxDir] = uigetfile({'*.bin', 'Binary capture file (*.bin)'; '*.*', 'All Files'}, ...
    '选择接收回波 cpi_*.bin (可按住 Ctrl 或 Shift 进行多选)', ...
    'MultiSelect', 'on');
if isequal(rxFileNames, 0)
    disp('已取消选择接收回波文件。');
    return;
end

% 统一为 cell 数组以方便后续统一处理
if ischar(rxFileNames)
    rxFileNames = {rxFileNames};
end

%% 2. 加载并解析数据
fprintf('正在读取并解析雷达原始双向信号...\n');
try
    [tx, rx, rf_param] = load_radar_cpi(txFile, rxFileNames, rxDir);
    
    % 显示数据维度
    fprintf('\n================== 解析成功 ==================\n');
    fprintf('  发射信号 tx.data 维度: [%d x %d] (一维列向量)\n', size(tx.data, 1), size(tx.data, 2));
    fprintf('  接收逻辑通道数: %d\n', numel(rx.param.channels));
    for c = 1:numel(rx.param.channels)
        chId = rx.param.channels(c);
        fieldName = sprintf('ch%d', chId);
        fprintf('    通道 %d (rx.%s) 维度: [%d x %d] (一维列向量)\n', ...
            chId, fieldName, size(rx.(fieldName), 1), size(rx.(fieldName), 2));
    end
    fprintf('  系统参数: Fs = %.2f MHz, PRI = %d 点\n', ...
        rx.param.sample_rate / 1e6, rx.param.pri_len);
    if isfield(rf_param, 'type')
        fprintf('  射频波形: 波形类型 = %s', rf_param.type);
        if isfield(rf_param, 'bandwidth_mhz')
            fprintf(', 带宽 = %.2f MHz', rf_param.bandwidth_mhz);
        end
        if isfield(rf_param, 'PW')
            fprintf(', 脉宽点数 = %d', rf_param.PW);
        end
        fprintf('\n');
    end
    
    % 自动导出数据为三个分立的 MAT 文件，并加入时间戳防止二次运行被覆盖
    ts = datestr(now, 'yyyymmdd_HHMMSS');
    txFilePath = fullfile(rxDir, sprintf('tx_%s.mat', ts));
    rxFilePath = fullfile(rxDir, sprintf('rx_%s.mat', ts));
    rfFilePath = fullfile(rxDir, sprintf('rf_param_%s.mat', ts));
    
    save(txFilePath, 'tx');
    save(rxFilePath, 'rx');
    save(rfFilePath, 'rf_param');
    
    fprintf('  已自动导出分立的 MAT 数据文件:\n');
    fprintf('    -> 发射参考 (tx): %s\n', txFilePath);
    fprintf('    -> 接收回波 (rx): %s\n', rxFilePath);
    fprintf('    -> 射频参数 (rf_param): %s\n', rfFilePath);
    fprintf('==============================================\n');
    fprintf('工作区变量已自动清理，目前仅留存 `tx`、`rx` 和 `rf_param`。\n\n');
    
catch ME
    error('雷达双向数据解析失败: %s', ME.message);
end

% 清理除了需要的 tx, rx, rf_param 之外的所有工作区临时变量
clearvars -except tx rx rf_param;

% =========================================================================
%                             本地函数定义
% =========================================================================

function [tx, rx, rf_param] = load_radar_cpi(txFile, rxFileNames, rxDir, varargin)
% 本地函数：同时加载发射参考信号与接收回波数据并打包为结构体
    selectedChannels = [];
    dataType = 'single';

    for idx = 1:2:length(varargin)
        name = varargin{idx};
        val = varargin{idx+1};
        if strcmpi(name, 'channels')
            selectedChannels = val;
        elseif strcmpi(name, 'dataType')
            dataType = val;
        end
    end

    % 1. 读取发射侧 metadata.json
    [txDir, txFileNameOnly, ~] = fileparts(txFile);
    txMetadataPath = fullfile(txDir, 'metadata.json');
    if ~exist(txMetadataPath, 'file')
        error('发射参考所在目录缺少 metadata.json: %s', txMetadataPath);
    end
    txMeta = read_json_file(txMetadataPath);
    
    % 核心参数以发射侧为准
    fs = double(txMeta.sample_rate);
    priLen = double(txMeta.PRI);
    
    txFormat = get_optional_string_field(txMeta, 'format', 'CS16');
    if ~strcmpi(txFormat, 'CS16')
        error('只支持发射参考格式为 CS16，当前为 %s。', txFormat);
    end
    txChannels = get_optional_numeric_array_field(txMeta, 'channels', 0);
    txNumChannels = max(numel(txChannels), 1);

    % 2. 读取接收侧 metadata.json
    rxMetadataPath = fullfile(rxDir, 'metadata.json');
    if ~exist(rxMetadataPath, 'file')
        error('接收 capture 目录缺少 metadata.json: %s', rxMetadataPath);
    end
    rxMeta = read_json_file(rxMetadataPath);
    
    % 接收排布以接收侧为准
    rxFormat = get_required_string_field(rxMeta, 'format', rxMetadataPath);
    if ~strcmpi(rxFormat, 'CS16')
        error('只支持接收格式为 CS16，当前为 %s。', rxFormat);
    end
    allChannels = double(rxMeta.channels(:)).';

    if isempty(selectedChannels)
        selectedChannels = allChannels;
    end

    % 3. 读取并提取发射参考信号
    txRaw = read_cs16_channel_file(txFile, txNumChannels, 1);
    if numel(txRaw) < priLen
        error('发射参考点数 %d 小于配置 of PRI 点数 %d。', numel(txRaw), priLen);
    elseif numel(txRaw) > priLen
        txRaw = txRaw(1:priLen);
    end

    activeIdx = find(abs(txRaw) > 0);
    if isempty(activeIdx)
        error('发射参考文件中没有找到有效的非零信号。');
    end
    txRef = txRaw(activeIdx(1):activeIdx(end));
    txRef = txRef(:);
    
    % 发射参考信号归一化，留存有效归一化 LFM 段（可选）
    txRefNorm = txRef / sqrt(sum(abs(txRef).^2));

    % 打包 tx 结构体，数据强制转换为一维列向量
    tx = struct();
    tx.param = struct(...
        'sample_rate', fs, ...
        'pri_len', priLen, ...
        'channels', txChannels, ...
        'file_name', [txFileNameOnly, '.bin']);
    tx.data = cast(txRaw(:), dataType);

    % 4. 按照文件名中的 CPI 序号对接收文件进行升序排序，保证拼接时时间轴的连续性
    cpiNums = cellfun(@(x) str2double(regexp(x, '\d+', 'match', 'once')), rxFileNames);
    if any(isnan(cpiNums))
        % 如果有些文件名中没有匹配到数字，则不做重新排序，保留用户的选择顺序
    else
        [~, orderIdx] = sort(cpiNums);
        rxFileNames = rxFileNames(orderIdx);
    end

    % 5. 读取并按照发射侧的 priLen 对接收数据进行解交织，并将多 CPI 拼接为一维连续时间序列
    numRxChannels = numel(allChannels);
    
    % 预先分配 cell 矩阵，每行代表一个文件，每列代表一个通道，避免在循环中动态增长
    cpiDataCells = cell(numel(rxFileNames), numel(selectedChannels));
    totalPriAccum = 0;
    
    for fileIdx = 1:numel(rxFileNames)
        filePath = fullfile(rxDir, rxFileNames{fileIdx});
        fid = fopen(filePath, 'rb');
        if fid == -1
            error('无法打开接收回波文件: %s', filePath);
        end
        raw = fread(fid, inf, 'int16=>single', 0, 'ieee-le');
        fclose(fid);

        totalSamples = numel(raw) / (2 * numRxChannels);
        totalPri = floor(totalSamples / priLen);

        if totalPri < 1
            error('回波数据长度不足一个 PRI: %s', filePath);
        end

        rawMat = reshape(raw(1:(totalPri * priLen * numRxChannels * 2)), 2 * numRxChannels, []);
        clear raw;

        for chPosIdx = 1:numel(selectedChannels)
            ch = selectedChannels(chPosIdx);
            chPos = find(allChannels == ch, 1);
            if isempty(chPos)
                error('指定的接收通道 %d 不在通道列表 [%s] 内。', ch, mat2str(allChannels));
            end

            iRow = (chPos - 1) * 2 + 1;
            qRow = iRow + 1;

            rxComplex = complex(rawMat(iRow, :), rawMat(qRow, :));
            % 取出该通道在本 CPI 中的一维复信号并转为列向量
            cpiDataCells{fileIdx, chPosIdx} = rxComplex(1:(priLen * totalPri)).';
        end
        totalPriAccum = totalPriAccum + totalPri;
    end

    % 6. 将各文件中的数据合并，打包并赋值给 rx 结构体，数据强制转换为一维列向量
    rx = struct();
    rx.param = struct(...
        'sample_rate', fs, ...
        'pri_len', priLen, ...
        'prf', fs / priLen, ...
        'channels', selectedChannels, ...
        'total_pri', totalPriAccum, ...
        'total_samples', totalPriAccum * priLen, ...
        'cpi_files', {rxFileNames});

    for chPosIdx = 1:numel(selectedChannels)
        chId = selectedChannels(chPosIdx);
        % 把该通道在各文件中的一维列向量按时间拼接，合并后直接拉直为一维列向量 (M * 1)
        combinedVector = cat(1, cpiDataCells{:, chPosIdx});
        fieldName = sprintf('ch%d', chId);
        rx.(fieldName) = cast(combinedVector(:), dataType);
    end
    
    % 7. 提取发射元数据中的波形射频参数作为 rf_param 返回
    if isfield(txMeta, 'waveform')
        rf_param = txMeta.waveform;
    else
        rf_param = struct();
    end
end

%% 辅助本地函数
function meta = read_json_file(filePath)
    try
        txt = fileread(filePath);
        meta = jsondecode(txt);
    catch ME
        error('读取或解析 JSON 失败: %s\n%s', filePath, ME.message);
    end
end

function charVal = string_to_char(val)
    if isstring(val)
        charVal = char(val);
    elseif ischar(val)
        charVal = val;
    else
        error('参数类型错误，期望字符/字符串型。');
    end
end

function x = read_cs16_channel_file(filePath, numChannels, channelPos)
    fid = fopen(filePath, 'rb');
    if fid == -1
        error('无法打开文件: %s', filePath);
    end
    raw = fread(fid, inf, 'int16=>single', 0, 'ieee-le');
    fclose(fid);

    intsPerTimeSample = 2 * numChannels;
    if mod(numel(raw), intsPerTimeSample) ~= 0
        error('二进制文件长度与通道数不匹配: %s', filePath);
    end

    rawMat = reshape(raw, intsPerTimeSample, []);
    iRow = (channelPos - 1) * 2 + 1;
    qRow = iRow + 1;
    x = complex(rawMat(iRow, :), rawMat(qRow, :)).';
end

function value = get_required_string_field(meta, fieldName, sourcePath)
    if ~isfield(meta, fieldName)
        error('metadata 缺少字符串字段 "%s": %s', fieldName, sourcePath);
    end
    value = string_to_char(meta.(fieldName));
end

function value = get_optional_string_field(meta, fieldName, defaultValue)
    if ~isfield(meta, fieldName)
        value = defaultValue;
        return;
    end
    value = string_to_char(meta.(fieldName));
end

function arr = get_required_numeric_array_field(meta, fieldName, sourcePath)
    if ~isfield(meta, fieldName) || ~isnumeric(meta.(fieldName))
        error('metadata 缺少数值数组字段 "%s": %s', fieldName, sourcePath);
    end
    arr = double(meta.(fieldName)(:)).';
end

function arr = get_optional_numeric_array_field(meta, fieldName, defaultValue)
    if ~isfield(meta, fieldName) || ~isnumeric(meta.(fieldName))
        arr = defaultValue;
        return;
    end
    arr = double(meta.(fieldName)(:)).';
end
