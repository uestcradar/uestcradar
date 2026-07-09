function [rdNoMtiDbDisplay, rdWeakMtiDbDisplay, zeroNoMtiDbDisplay, zeroWeakMtiDbDisplay, ...
    weakDopplerCutDbDisplay, weakPeakRange, weakPeakVelocity, weakPeakDisplayRow] = ...
    process_one_window(rxFile, priLen, startPri, coherentPri, matchedFilter, refLen, ...
    slowWindow, mtiStrength, enableFastTimeDcRemove, rangeDisplayIdx, ...
    velocityDisplayIdx, nonzeroRows, rangeAxis, velocityAxis, ...
    numRxChannels, selectedChannelPos, ...
    guard_r, guard_d, train_r, train_d, pfa_log, rangeZeroBin)
% PROCESS_ONE_WINDOW 处理一个滑窗数据，返回四宫格谱图和最强目标位置点
%   通过调用封装的算法子函数（脉压、MTI、Doppler FFT、CFAR检测）实现模块化处理。

    % 1. 读取指定的连续 PRI 数据块（使用带偏置的零点读取）
    rxMat = read_contiguous_pri_block(rxFile, priLen, startPri, coherentPri, ...
        numRxChannels, selectedChannelPos, rangeZeroBin);

    % 去除快时间 DC 偏置
    if enableFastTimeDcRemove
        rxMat = rxMat - mean(rxMat, 2);
    end

    % 2. 脉冲压缩（时域匹配滤波）
    pc = pulse_compression(rxMat, matchedFilter, refLen, priLen);
    clear rxMat;
    
    % 截取脉压结果，去除匹配滤波器带来的群延迟，使得直漏信号零点对准第一点 (0米)
    rangeProfiles = pc(:, refLen : (refLen + priLen - 1));
    clear pc;

    % 3. MTI 静态杂波抑制（使用平均对消 MTI 滤波器）
    rangeProfilesMti = mti_avg(rangeProfiles, mtiStrength);

    % 4. 多普勒处理（Doppler FFT，得到二维功率谱）
    rdNoMtiPower = doppler_fft(rangeProfiles, slowWindow, coherentPri);
    rdWeakMtiPower = doppler_fft(rangeProfilesMti, slowWindow, coherentPri);

    % 5. 归一化与分贝化（DB）计算，供 GUI 四宫格画图显示使用
    framePowerRef = max(rdNoMtiPower(:));
    rdNoMtiDb = 10 * log10(rdNoMtiPower / framePowerRef + eps);
    rdWeakMtiDb = 10 * log10(rdWeakMtiPower / framePowerRef + eps);

    zeroDopplerRow = coherentPri / 2 + 1;
    zeroNoMtiDb = 10 * log10(rdNoMtiPower(zeroDopplerRow, :) / framePowerRef + eps);
    zeroWeakMtiDb = 10 * log10(rdWeakMtiPower(zeroDopplerRow, :) / framePowerRef + eps);

    % 提取局部显示区间的数据
    rdNoMtiDbDisplay = rdNoMtiDb(velocityDisplayIdx, rangeDisplayIdx);
    rdWeakMtiDbDisplay = rdWeakMtiDb(velocityDisplayIdx, rangeDisplayIdx);
    zeroNoMtiDbDisplay = zeroNoMtiDb(rangeDisplayIdx);
    zeroWeakMtiDbDisplay = zeroWeakMtiDb(rangeDisplayIdx);

    % 6. 恒虚警目标检测 (2D CA-CFAR)
    guard_size = [guard_r, guard_d];
    train_size = [train_r, train_d];
    pfa = 10^(pfa_log);
    
    [~, weakPeakRange, weakPeakVelocity, weakPeakFullVelocityRow, weakPeakFullRangeCol] = cfar_detector(...
        rdWeakMtiPower, rangeAxis, velocityAxis, nonzeroRows, rangeDisplayIdx, ...
        guard_size, train_size, pfa);

    % 计算目标在显示多普勒维的行索引
    weakPeakDisplayRow = find(velocityDisplayIdx == weakPeakFullVelocityRow, 1);

    % 提取多普勒切片数据
    weakDopplerCutDb = 10 * log10(rdWeakMtiPower(:, weakPeakFullRangeCol) / framePowerRef + eps);
    weakDopplerCutDbDisplay = weakDopplerCutDb(velocityDisplayIdx);
end
