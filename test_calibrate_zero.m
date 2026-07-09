%% test_calibrate_zero.m
% 测试和分析距离零点自动标定（脉压互相关峰值）的调试脚本。
% 运行此脚本将绘制出脉冲压缩后的相关包络图，帮助您直观查看存在多少个峰值，
% 以及为什么原有的搜索算法会误定位到 2563。

clear; clc; close all;

% 将 algorithm 子目录加入路径
scriptDir = fileparts(mfilename('fullpath'));
addpath(fullfile(scriptDir, 'algorithm'));

%% 1. 选择文件和目录
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
if ~exist(txMetadataPath, 'file') || ~exist(rxMetadataPath, 'file')
    error('缺少 metadata.json 配置文件。');
end

txMeta = read_json_file(txMetadataPath);
rxMeta = read_json_file(rxMetadataPath);

%% 2. 解析基本参数
fs = double(txMeta.sample_rate);
priLen = double(txMeta.PRI);
fc = 9.5e9;
rangePerBin = 299792458 / (2 * fs);

txChannels = txMeta.channels;
txNumChannels = max(numel(txChannels), 1);
rxChannels = rxMeta.channels;
numRxChannels = numel(rxChannels);

selectedChannelPos = 1; % 默认处理第 1 个通道

%% 3. 读取并准备 TX 匹配滤波器
txRaw = read_cs16_channel_file(txFile, txNumChannels, 1);
if numel(txRaw) > priLen
    txRaw = txRaw(1:priLen);
end
activeIdx = find(abs(txRaw) > 0);
txRef = txRaw(activeIdx(1):activeIdx(end));
txRef = txRef(:);
refLen = numel(txRef);
txRefNorm = txRef / sqrt(sum(abs(txRef).^2));
matchedFilter = conj(flipud(txRefNorm));

%% 4. 读取首个 CPI 数据进行分析
cpiFiles = dir(fullfile(dataDir, 'cpi_*.bin'));
[~, cpiOrder] = sort({cpiFiles.name});
firstCpiFile = fullfile(dataDir, cpiFiles(cpiOrder(1)).name);

coherentPri = 256;
calPriNum = min(64, coherentPri);

% 读取两倍数量的 PRI，用于在快时间维上拼接成两倍长度 (2 * priLen)
rxCalMat_raw = read_contiguous_pri_block(firstCpiFile, priLen, 1, 2 * calPriNum, ...
    numRxChannels, selectedChannelPos);

% 将相邻的两个 1024 点脉冲在快时间维度上拼接成一个 2048 点的脉冲
rxCalMat = zeros(calPriNum, 2 * priLen);
for p = 1:calPriNum
    rxCalMat(p, :) = [rxCalMat_raw(2*p - 1, :), rxCalMat_raw(2*p, :)];
end

% 去除快时间 DC
rxCalMat_noDc = rxCalMat - mean(rxCalMat, 2);

% 脉冲压缩
pcCal = conv2(rxCalMat_noDc, matchedFilter.', 'full');
calProfiles = pcCal;

%% 5. 分别计算两种积累方式的曲线
% 方法 A：原有算法（非相干平均：先取模值，再对脉冲平均）
profile_incoherent = mean(abs(calProfiles), 1);

% 方法 B：新算法（静默期滑动积分 + 截取单周期波形 + 脉压首峰 + mod 1024 取模）
% 1. 在原始接收信号上寻找第一个周期的静默期
meanRawProfile = mean(abs(rxCalMat_raw), 1);
winLen_raw = 128;
energySum_raw = conv(meanRawProfile, ones(1, winLen_raw), 'valid');
searchRange_raw = 1:min(priLen, numel(energySum_raw));
[~, minOffset] = min(energySum_raw(searchRange_raw));
minIdx = searchRange_raw(minOffset);

% 2. 截取单周期信号 rx_window
rx_window = zeros(calPriNum, priLen);
for p = 1:calPriNum
    double_pulse = [rxCalMat_raw(2*p - 1, :), rxCalMat_raw(2*p, :)];
    rx_window(p, :) = double_pulse(minIdx : minIdx + priLen - 1);
end
rx_window_noDc = rx_window - mean(rx_window, 2);

% 3. 脉冲压缩（只对 1024 点的 rx_window_noDc 进行）
pcCal_win = conv2(rx_window_noDc, matchedFilter.', 'full');
profile_coherent = abs(mean(pcCal_win, 1));

%% 6. 执行搜索定位
% 6.1 原有非相干搜索（无前沿避让）
[~, peakIdx_old] = max(profile_incoherent);
bin_old = peakIdx_old - 1;

% 6.2 新相干搜索（寻找第一个过门限的局部极大值）
noiseFloor = median(profile_coherent);
threshold = 5 * noiseFloor; % 自适应门限设为噪声中位数的 5 倍

peakIdx_new = [];
for i = 2 : (numel(profile_coherent) - 1)
    if profile_coherent(i) > threshold && ...
       profile_coherent(i) >= profile_coherent(i-1) && ...
       profile_coherent(i) >= profile_coherent(i+1)
        peakIdx_new = i;
        break;
    end
end

if isempty(peakIdx_new)
    [~, peakIdx_new] = max(profile_coherent);
end
bin_new = mod(minIdx + peakIdx_new - refLen, priLen);

fprintf('================ 标定对比结果 ================\n');
fprintf('  原有非相干估计值 (全局 Max) : bin = %d (对应距离: %.2f m)\n', bin_old, bin_old * rangePerBin);
fprintf('  新相干估计值 (自适应首峰法) : bin = %d (对应距离: %.2f m)\n', bin_new, bin_new * rangePerBin);
fprintf('  原硬编码默认零点值           : bin = 2150 (对应距离: %.2f m)\n', 2150 * rangePerBin);
fprintf('==============================================\n');

%% 7. 绘制互相关包络对比图
figure('Color', 'w', 'Name', '距离零点标定分析（相关峰值）', 'Position', [100, 100, 1000, 600]);

% 子图 1：原有非相干积累曲线（双倍长度拼接未裁剪）
subplot(2, 1, 1);
plot(0:size(profile_incoherent, 2)-1, profile_incoherent, 'b', 'LineWidth', 1.2); hold on;
plot(bin_old, profile_incoherent(peakIdx_old), 'ro', 'MarkerSize', 8, 'MarkerFaceColor', 'r');
grid on;
title('【方法 A】原算法：双倍长度拼接非相干平均曲线 mean(abs(x))');
xlabel('快时间采样点索引 (Range Bin Index)');
ylabel('相关幅值');
legend('互相关包络', sprintf('算法全局峰点 (Bin %d)', bin_old), 'Location', 'northeast');

% 子图 2：优化后的单周期相干积累与首峰定位曲线
subplot(2, 1, 2);
plot(0:size(profile_coherent, 2)-1, profile_coherent, 'g', 'LineWidth', 1.2); hold on;
% 标出找到的直漏峰
plot(peakIdx_new - 1, profile_coherent(peakIdx_new), 'ko', 'MarkerSize', 8, 'MarkerFaceColor', 'k');
% 画出自适应噪声门限
yline(threshold, 'm--', sprintf('自适应噪声门限 (%.2f)', threshold), 'LineWidth', 1.2);
grid on;
title(sprintf('【方法 B】新算法：截取单周期相干积累脉压曲线（从静默区起点 %d 截取）', minIdx - 1));
xlabel('单周期内快时间采样点索引 (Window Range Bin Index)');
ylabel('相关幅值');
legend('互相关包络', sprintf('直漏峰 (Bin %d，绝对零点为 Bin %d)', peakIdx_new - 1, bin_new), 'Location', 'northeast');

sgtitle('雷达互相关（脉压）波形与零点搜索分析');
