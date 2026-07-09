function rangeZeroBin = calibrate_range_zero(filePath, priLen, coherentPri, ...
    numRxChannels, selectedChannelPos, matchedFilter, refLen)
% CALIBRATE_RANGE_ZERO 自动标定直漏信号距离零点位置（首个过门限局部峰值自适应标定）
%
%   输入参数：
%     filePath           : 用于标定的接收二进制文件路径 (通常是第一个 CPI 文件)
%     priLen             : 每个 PRI 的采样点数
%     coherentPri        : 相干处理的 PRI 个数
%     numRxChannels      : 总接收通道数
%     selectedChannelPos : 当前处理的通道在通道列表中的索引 (1-based)
%     matchedFilter      : 脉冲压缩匹配滤波器
%     refLen             : 发射参考信号的有效点数
%
%   输出参数：
%     rangeZeroBin       : 估计出的直漏信号对应的 range bin 索引 (0-based)

    % 限制用于标定的脉冲数，降低内存和计算开销
    calPriNum = min(64, coherentPri);
    
    % 1. 读取双倍数量的 PRI，用于在快时间维上拼接成两倍长度 (2 * priLen)
    rxCalMat_raw = read_contiguous_pri_block(filePath, priLen, 1, 2 * calPriNum, ...
        numRxChannels, selectedChannelPos);
    
    % 2. 在原始接收信号上寻找第一个周期的静默期
    %    计算脉冲平均后的原始幅度曲线 (1 x priLen)
    meanRawProfile = mean(abs(rxCalMat_raw), 1);
    
    %    设定滑动窗口宽度 winLen = 128 点
    winLen = 128;
    energySum = conv(meanRawProfile, ones(1, winLen), 'valid');
    
    %    寻找积分累加和最小的起始位置，限制在前 1 个 PRI 范围内
    searchRange = 1:min(priLen, numel(energySum));
    [~, minOffset] = min(energySum(searchRange));
    minIdx = searchRange(minOffset);
    
    % 3. 截取单周期信号 rx_window
    %    每个脉冲都截取从 minIdx 开始、长度为 priLen 的段，保证包含且仅包含一个完整的发射脉冲
    rx_window = zeros(calPriNum, priLen);
    for p = 1:calPriNum
        double_pulse = [rxCalMat_raw(2*p - 1, :), rxCalMat_raw(2*p, :)];
        rx_window(p, :) = double_pulse(minIdx : minIdx + priLen - 1);
    end
    
    % 4. 去除快时间 DC 偏置
    rx_window = rx_window - mean(rx_window, 2);
    
    % 5. 脉冲压缩（只对 1024 点的 rx_window 进行，输出为 1087 点）
    pcCal = conv2(rx_window, matchedFilter.', 'full');
    
    % 6. 相干积累 (Coherent Integration)
    meanProfile = abs(mean(pcCal, 1));
    
    % 7. 搜索第一个过门限的局部极大值（直漏峰）
    noiseFloor = median(meanProfile);
    threshold = 5 * noiseFloor; % 自适应门限设为噪声中位数的 5 倍
    
    peakIdx = [];
    for i = 2 : (numel(meanProfile) - 1)
        if meanProfile(i) > threshold && ...
           meanProfile(i) >= meanProfile(i-1) && ...
           meanProfile(i) >= meanProfile(i+1)
            peakIdx = i;
            break; % 找到第一个过门限的局部极大值，立即退出
        end
    end
    
    % 安全降级机制：如果未找到任何过门限峰值，降级使用全局极大值
    if isempty(peakIdx)
        [~, peakIdx] = max(meanProfile);
    end
    
    % 8. 取模 priLen 计算绝对零点
    rangeZeroBin = mod(minIdx + peakIdx - refLen, priLen);
end
