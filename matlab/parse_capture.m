%% 第 6 步：滑窗 Range-Doppler 动画
% 本脚本把第 5 步的“四宫格 Range-Doppler 对比图”做成滑窗动画。
%
% 这样做的原因：
%   第 5 步只能看一个 256 PRI 相干处理块。
%   如果某个亮点真的是无人机，它应该在相邻处理窗口中连续出现，
%   并且距离、速度随时间有相对平滑的变化。
%   只把每帧压缩成一个“候选点轨迹”容易误判，也容易丢掉 RD 图中的上下文。
%
% 本脚本每一帧显示的内容与第 5 步一致：
%   左上：无 MTI 的 Range-Doppler 图。
%   右上：弱 MTI 后的 Range-Doppler 图。
%   左下：零多普勒距离切片，无 MTI 与弱 MTI 对比。
%   右下：弱 MTI 后非零多普勒最强距离 bin 的 Doppler 切片。
%
% 观察重点：
%   1. 10 m/s 附近的亮点是否在相邻窗口中连续存在；
%   2. 该亮点的距离是否逐帧连续变化，而不是随机跳变；
%   3. 如果无人机径向往返飞行，距离变化趋势应在某些时段发生反向；
%   4. 与其相伴的速度应相对稳定，而不是每帧乱跳；
%   5. 如果某个亮点长期固定在同一个距离/速度，或者与 ±40 m/s 对称结构绑定，
%      更可能是系统杂散、旁瓣、强静态反射泄漏或其他非无人机目标。
%
% 默认不保存任何文件，只弹出 MATLAB 窗口循环播放。
% 如果确实需要保存 GIF，把 saveGif 改成 true。

clear; clc; close all;

% 将 algorithm 子目录加入路径
scriptDir = fileparts(mfilename('fullpath'));
addpath(fullfile(scriptDir, 'algorithm'));


%% 一、每次运行手动选择 TX 文件和 RX capture 目录
[txFileName, txDir] = uigetfile({'*.bin', 'Binary waveform (*.bin)'; '*.*', 'All Files'}, ...
    '选择发射参考 txFile (*.bin)');
if isequal(txFileName, 0)
    error('已取消选择发射参考文件。');
end
txFile = fullfile(txDir, txFileName);

dataDir = uigetdir(pwd, '选择接收 capture 文件夹（包含 metadata.json 和 cpi_*.bin）');
if isequal(dataDir, 0)
    error('已取消选择接收 capture 文件夹。');
end

txMetadataPath = fullfile(txDir, 'metadata.json');
rxMetadataPath = fullfile(dataDir, 'metadata.json');
if ~exist(txMetadataPath, 'file')
    error('发射参考文件所在目录缺少 metadata.json: %s', txMetadataPath);
end
if ~exist(rxMetadataPath, 'file')
    error('接收 capture 目录缺少 metadata.json: %s', rxMetadataPath);
end

txMeta = read_json_file(txMetadataPath);
rxMeta = read_json_file(rxMetadataPath);

%% 二、实验参数
% 按要求：实验参数以 txFile 所在目录的 metadata.json 为标准。
fs = get_required_numeric_field(txMeta, 'sample_rate', txMetadataPath);
priLen = get_required_numeric_field(txMeta, 'PRI', txMetadataPath);
priLen = double(priLen);
prf = fs / priLen;

fc = 9.5e9;


txFormat = get_optional_string_field(txMeta, 'format', 'CS16');
if ~strcmpi(txFormat, 'CS16')
    error('当前脚本只支持 TX format=CS16，实际为 %s。', txFormat);
end
txChannels = get_optional_numeric_array_field(txMeta, 'channels', 0);
txNumChannels = max(numel(txChannels), 1);

% 按要求：RX 二进制数据排布以接收目录 metadata.json 中的 format/channels 为准。
rxFormat = get_required_string_field(rxMeta, 'format', rxMetadataPath);
if ~strcmpi(rxFormat, 'CS16')
    error('当前脚本只支持 RX format=CS16，实际为 %s。', rxFormat);
end
rxChannels = get_required_numeric_array_field(rxMeta, 'channels', rxMetadataPath);
numRxChannels = numel(rxChannels);
if numRxChannels < 1
    error('RX metadata channels 为空，无法确定接收数据排布。');
end

if numRxChannels == 1
    selectedChannelPos = 1;
else
    channelLabels = arrayfun(@(ch) sprintf('通道 %d', ch), rxChannels, 'UniformOutput', false);
    [selectedChannelPos, ok] = listdlg('PromptString', '选择用于 RD 处理的接收通道:', ...
        'SelectionMode', 'single', 'ListString', channelLabels, ...
        'InitialValue', 1, 'Name', '选择接收通道');
    if ok == 0
        error('已取消选择接收通道。');
    end
end
selectedChannelId = rxChannels(selectedChannelPos);

c0 = 299792458;
lambda = c0 / fc;
rangePerBin = c0 / (2 * fs);

% 距离零点校正参数：将通过下方互相关自动标定获取。

%% 三、选择要播放的数据范围
% 默认自动播放接收目录下按文件名排序的所有 cpi_*.bin。
cpiFiles = dir(fullfile(dataDir, 'cpi_*.bin'));
if isempty(cpiFiles)
    error('接收目录中没有找到 cpi_*.bin 文件: %s', dataDir);
end
[~, cpiOrder] = sort({cpiFiles.name});
cpiFiles = cpiFiles(cpiOrder);
cpiFileNames = {cpiFiles.name};

% 每帧 RD 使用的相干 PRI 数。
% 256 点速度分辨率约 0.46 m/s，是当前默认值。
coherentPri = 256;

% 滑窗步进。
% 当前每个 cpi 文件有 8192 个 PRI，半个 CPI 为 4096 个 PRI。
% 这里默认每次跨半个 CPI 显示一帧，使动画更快扫完整组数据，
% 便于观察亮点是否随时间发生明显距离移动。
%
% 可选设置：
%   128  ：相邻帧重叠 50%，动画细但很慢；
%   256  ：相邻帧不重叠，仍然较细；
%   4096 ：每次跨半个 CPI，默认推荐用于快速观察长时间变化；
%   8192 ：每个 CPI 只取一帧，最快但可能漏掉中间变化。
hopPri = 4096;

% 弱 MTI 强度，沿用第 5 步。
% 0.95 表示减去 95% 的慢时间复均值。
mtiStrength = 0.95;

% 是否做每个 PRI 内的快时间 DC 去除。
% 这不是 MTI，只是去掉每个 PRI 内的近似常量 IQ 偏置。
enableFastTimeDcRemove = true;

%% 三、显示和动画参数
% 只显示粗校正后的这个距离范围，便于观察可疑亮点。
% 如果想看全距离范围，可改成 [-10000, 10000] 或者更宽。
displayRangeLimitsMeters = [-500, 5000];

% 速度显示范围。
displayVelocityLimits = [-60, 60];

% 图像动态范围，单位 dB。
displayDynamicRangeDb = 70;

% 标记非零多普勒最强点时排除零速度附近。
nonzeroVelocityGuard = 0.8;

% 播放控制。
% loopForever 为 true 时，播完所有滑窗后从头循环，直到手动关闭图窗。
pauseSeconds = 0.05;
loopForever = get_default_loop_forever();

% 默认不保存 GIF。
% 如果要保存动图，把 saveGif 改成 true。
% 注意保存 GIF 会显著降低播放速度。
saveGif = get_default_save_gif();
gifFile = fullfile(dataDir, 'step06_sliding_rd_animation.gif');
gifDelaySeconds = 0.08;

fprintf('第 6 步：滑窗 Range-Doppler 动画\n');
fprintf('  数据目录: %s\n', dataDir);
fprintf('  发射参考: %s\n', txFile);
fprintf('  TX metadata: %s\n', txMetadataPath);
fprintf('  RX metadata: %s\n', rxMetadataPath);
fprintf('  Fs: %.2f MHz, PRF: %.2f Hz, PRI长度: %d 点\n', fs / 1e6, prf, priLen);
fprintf('  载频: %.3f GHz, 波长: %.4f m\n', fc / 1e9, lambda);
fprintf('  TX format=%s, TX channels=%s\n', txFormat, mat2str(txChannels));
fprintf('  RX format=%s, RX channels=%s, 当前处理通道=%d（文件内第 %d 路）\n', ...
    rxFormat, mat2str(rxChannels), selectedChannelId, selectedChannelPos);
fprintf('  距离采样间隔: %.3f m/bin\n', rangePerBin);
fprintf('  播放 CPI 文件数: %d\n', numel(cpiFileNames));
fprintf('  coherentPri=%d, hopPri=%d, mtiStrength=%.2f\n', ...
    coherentPri, hopPri, mtiStrength);

%% 四、读取并准备 TX 匹配滤波参考
if ~exist(txFile, 'file')
    error('找不到发射参考文件: %s', txFile);
end

txRaw = read_cs16_channel_file(txFile, txNumChannels, 1);
if numel(txRaw) < priLen
    error('发射参考复采样点数为 %d，小于 TX metadata PRI 长度 %d。', numel(txRaw), priLen);
elseif numel(txRaw) > priLen
    warning('发射参考复采样点数为 %d，大于 PRI=%d，仅使用第一帧 PRI。', numel(txRaw), priLen);
    txRaw = txRaw(1:priLen);
end

activeIdx = find(abs(txRaw) > 0);
if isempty(activeIdx)
    error('发射参考文件中没有找到非零 LFM 样本。');
end

txRef = txRaw(activeIdx(1):activeIdx(end));
txRef = txRef(:);
refLen = numel(txRef);

% 参考信号能量归一化，匹配滤波器为共轭时间反转。
txRef = txRef / sqrt(sum(abs(txRef).^2));
matchedFilter = conj(flipud(txRef));

fprintf('  发射参考有效长度: %d 点\n', refLen);

%% 自动校准距离零点（寻找直漏信号最强峰值位置）
fprintf('正在自动校准距离零点...\n');
firstCpiFile = fullfile(dataDir, cpiFileNames{1});
rangeZeroBin = calibrate_range_zero(firstCpiFile, priLen, coherentPri, ...
    numRxChannels, selectedChannelPos, matchedFilter, refLen);
rangeZeroOffsetMeters = rangeZeroBin * rangePerBin;

fprintf('  自动检测到直漏峰值位于第 %d 个 bin\n', rangeZeroBin + 1);
fprintf('  自动标定零点偏置: rangeZeroBin = %d (扣除 %.1f m 延迟)\n', ...
    rangeZeroBin, rangeZeroOffsetMeters);

%% 五、构造距离轴和速度轴
delayBins = 0:(priLen - 1);
rangeAxis = (delayBins - rangeZeroBin) * rangePerBin;

rangeDisplayMask = rangeAxis >= displayRangeLimitsMeters(1) & ...
                   rangeAxis <= displayRangeLimitsMeters(2);
rangeDisplayIdx = find(rangeDisplayMask);
rangeAxisDisplay = rangeAxis(rangeDisplayIdx);

if isempty(rangeDisplayIdx)
    error('显示距离范围内没有任何 range bin，请检查 displayRangeLimitsMeters。');
end

fdAxis = (-coherentPri/2:(coherentPri/2 - 1)) * (prf / coherentPri);
velocityAxis = lambda * fdAxis / 2;
zeroDopplerRow = coherentPri / 2 + 1;

velocityDisplayMask = velocityAxis >= displayVelocityLimits(1) & ...
                      velocityAxis <= displayVelocityLimits(2);
velocityDisplayIdx = find(velocityDisplayMask);
velocityAxisDisplay = velocityAxis(velocityDisplayIdx);

if isempty(velocityDisplayIdx)
    error('显示速度范围内没有任何 Doppler bin，请检查 displayVelocityLimits。');
end

nonzeroRows = abs(velocityAxis) >= nonzeroVelocityGuard & velocityDisplayMask;
if ~any(nonzeroRows)
    error('非零多普勒保护区外没有可用 Doppler bin，请检查速度显示范围。');
end

slowWindow = make_hann_window(coherentPri);

%% 六、预生成滑窗列表
% frameList 每一行定义一个动画帧：
%   第 1 列：cpiFiles 中的文件序号；
%   第 2 列：CPI 文件编号；
%   第 3 列：该 CPI 内的起始 PRI；
%   第 4 列：该 CPI 内的结束 PRI；
%   第 5 列：按文件连续拼接后的滑窗中心时间。
frameList = [];
globalPriOffset = 0;
bytesPerPri = priLen * numRxChannels * 2 * 2;

for fileCounter = 1:numel(cpiFiles)
    rxFileName = cpiFileNames{fileCounter};
    rxFile = fullfile(dataDir, rxFileName);
    cpiIndex = parse_cpi_index(rxFileName, fileCounter - 1);

    if ~exist(rxFile, 'file')
        warning('找不到文件，跳过: %s', rxFile);
        continue;
    end

    rxInfo = dir(rxFile);
    tailBytes = mod(rxInfo.bytes, bytesPerPri);
    if tailBytes ~= 0
        warning('文件尾部有 %d 字节不足一个完整 PRI，将直接截断: %s', tailBytes, rxFile);
    end

    totalPriInFile = floor(rxInfo.bytes / bytesPerPri);
    if totalPriInFile < coherentPri
        warning('文件完整 PRI 数 %d 小于 coherentPri=%d，跳过: %s', ...
            totalPriInFile, coherentPri, rxFile);
        continue;
    end

    windowStarts = 1:hopPri:(totalPriInFile - coherentPri + 1);

    for kk = 1:numel(windowStarts)
        startPri = windowStarts(kk);
        endPri = startPri + coherentPri - 1;
        centerPriInFile = startPri + (coherentPri - 1) / 2;
        centerTime = (globalPriOffset + centerPriInFile) / prf;

        frameList(end+1, :) = [fileCounter, cpiIndex, startPri, endPri, centerTime]; %#ok<SAGROW>
    end

    globalPriOffset = globalPriOffset + totalPriInFile;
end

if isempty(frameList)
    error('没有生成任何动画帧，请检查接收目录中的 cpi_*.bin、PRI 和通道 metadata。');
end

fprintf('  动画帧数: %d\n', size(frameList, 1));
fprintf('  粗校正显示距离范围: %.1f 到 %.1f m\n', ...
    displayRangeLimitsMeters(1), displayRangeLimitsMeters(2));
fprintf('  速度显示范围: %.1f 到 %.1f m/s\n', ...
    displayVelocityLimits(1), displayVelocityLimits(2));

%% 七、创建动画图窗
fig = figure('Color', 'w', ...
    'Name', '第 6 步：滑窗 Range-Doppler 动画', ...
    'NumberTitle', 'off', ...
    'Position', [60 50 1600 900]);

tl = tiledlayout(fig, 2, 2, 'TileSpacing', 'compact', 'Padding', 'compact');
axNoMti = nexttile(tl, 1);
axWeakMti = nexttile(tl, 2);
axZeroCut = nexttile(tl, 3);
axDopplerCut = nexttile(tl, 4);

gifFrameCounter = 0;

%% 八、逐滑窗播放第 5 步四宫格
while isvalid(fig)
    for frameIdx = 1:size(frameList, 1)
        if ~isvalid(fig)
            return;
        end

        cpiFilePos = frameList(frameIdx, 1);
        cpiIndex = frameList(frameIdx, 2);
        startPri = frameList(frameIdx, 3);
        endPri = frameList(frameIdx, 4);
        centerTime = frameList(frameIdx, 5);

        rxFile = fullfile(dataDir, cpiFileNames{cpiFilePos});

        [rdNoMtiDb, rdWeakMtiDb, zeroNoMtiDb, zeroWeakMtiDb, ...
            weakDopplerCutDb, weakPeakRange, weakPeakVelocity, weakPeakRow] = ...
            process_one_window(rxFile, priLen, startPri, coherentPri, ...
            matchedFilter, refLen, slowWindow, mtiStrength, ...
            enableFastTimeDcRemove, rangeDisplayIdx, velocityDisplayIdx, ...
            nonzeroRows, rangeAxis, velocityAxis, numRxChannels, selectedChannelPos);

        % 左上：无 MTI RD。
        cla(axNoMti);
        imagesc(axNoMti, rangeAxisDisplay, velocityAxisDisplay, rdNoMtiDb);
        axis(axNoMti, 'xy');
        colormap(axNoMti, parula);
        colorbar(axNoMti);
        clim(axNoMti, [-displayDynamicRangeDb, 0]);
        xlabel(axNoMti, '粗校正距离 (m)');
        ylabel(axNoMti, '径向速度 (m/s)');
        title(axNoMti, sprintf('无 MTI，cpi\\_%03d，PRI %d-%d，帧 %d/%d', ...
            cpiIndex, startPri, endPri, frameIdx, size(frameList, 1)));

        % 右上：弱 MTI RD。
        cla(axWeakMti);
        imagesc(axWeakMti, rangeAxisDisplay, velocityAxisDisplay, rdWeakMtiDb);
        axis(axWeakMti, 'xy');
        colormap(axWeakMti, parula);
        colorbar(axWeakMti);
        clim(axWeakMti, [-displayDynamicRangeDb, 0]);
        xlabel(axWeakMti, '粗校正距离 (m)');
        ylabel(axWeakMti, '径向速度 (m/s)');
        title(axWeakMti, sprintf('弱 MTI，中心时间 %.3f s，峰值 R=%.1f m, v=%.2f m/s', ...
            centerTime, weakPeakRange, weakPeakVelocity));
        hold(axWeakMti, 'on');
        plot(axWeakMti, weakPeakRange, weakPeakVelocity, 'ro', 'MarkerFaceColor', 'r');
        hold(axWeakMti, 'off');

        % 左下：零多普勒距离切片对比。
        cla(axZeroCut);
        plot(axZeroCut, rangeAxisDisplay, zeroNoMtiDb, 'k-', 'DisplayName', '无 MTI'); hold(axZeroCut, 'on');
        plot(axZeroCut, rangeAxisDisplay, zeroWeakMtiDb, 'r-', 'DisplayName', '弱 MTI');
        hold(axZeroCut, 'off');
        grid(axZeroCut, 'on');
        xlabel(axZeroCut, '粗校正距离 (m)');
        ylabel(axZeroCut, '零多普勒功率 (dB，本帧归一化)');
        ylim(axZeroCut, [-displayDynamicRangeDb, 3]);
        title(axZeroCut, '零多普勒距离切片对比');
        legend(axZeroCut, 'Location', 'northeast');

        % 右下：弱 MTI 非零多普勒最强距离 bin 的 Doppler 切片。
        cla(axDopplerCut);
        plot(axDopplerCut, velocityAxisDisplay, weakDopplerCutDb, 'b-'); hold(axDopplerCut, 'on');
        xline(axDopplerCut, 0, 'k--', '零速度');
        plot(axDopplerCut, weakPeakVelocity, weakDopplerCutDb(weakPeakRow), ...
            'ro', 'MarkerFaceColor', 'r');
        hold(axDopplerCut, 'off');
        grid(axDopplerCut, 'on');
        xlabel(axDopplerCut, '径向速度 (m/s)');
        ylabel(axDopplerCut, '功率 (dB，本帧归一化)');
        ylim(axDopplerCut, [-displayDynamicRangeDb, 3]);
        title(axDopplerCut, sprintf('弱 MTI 非零多普勒切片，R=%.1f m', weakPeakRange));

        sgtitle(tl, sprintf('滑窗 RD 动画：cpi\\_%03d，PRI %d-%d，中心时间 %.3f s', ...
            cpiIndex, startPri, endPri, centerTime));

        drawnow;

        if saveGif && isvalid(fig)
            gifFrameCounter = gifFrameCounter + 1;
            frame = getframe(fig);
            [imind, cm] = rgb2ind(frame2im(frame), 256);
            if gifFrameCounter == 1
                imwrite(imind, cm, gifFile, 'gif', 'Loopcount', inf, ...
                    'DelayTime', gifDelaySeconds);
            else
                imwrite(imind, cm, gifFile, 'gif', 'WriteMode', 'append', ...
                    'DelayTime', gifDelaySeconds);
            end
        end

        pause(pauseSeconds);
    end

    if ~loopForever
        break;
    end
end

if saveGif
    fprintf('GIF 已保存: %s\n', gifFile);
else
    fprintf('动画播放结束。未保存任何文件。\n');
end

%% 本脚本用到的本地函数
function value = get_default_loop_forever()
    value = true;
end

function value = get_default_save_gif()
    value = false;
end



function value = get_required_numeric_field(meta, fieldName, sourcePath)
    if ~isfield(meta, fieldName) || ~isnumeric(meta.(fieldName))
        error('metadata 缺少数值字段 "%s": %s', fieldName, sourcePath);
    end
    value = double(meta.(fieldName));
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

function value = string_to_char(value)
    if isstring(value)
        value = char(value);
    elseif ischar(value)
        % keep as-is
    else
        error('字段不是字符串。');
    end
end

function cpiIndex = parse_cpi_index(fileName, fallbackIndex)
    token = regexp(fileName, '^cpi_(\d+)\.bin$', 'tokens', 'once');
    if isempty(token)
        cpiIndex = fallbackIndex;
    else
        cpiIndex = str2double(token{1});
    end
end


