function y = doppler_fft(x, w, n_fft)
% DOPPLER_FFT 雷达慢时间多普勒 FFT 处理（加窗、FFT、fftshift、求功率谱）
%   y = doppler_fft(x, w, n_fft)
%
%   输入参数：
%     x     : 输入的距离-脉冲时域信号矩阵（行数为脉冲/慢时间，列数为距离/快时间）
%     w     : 慢时间窗函数（加权向量，长度需与 x 的行数一致） [n_pulses x 1]
%     n_fft : 多普勒 FFT 点数
%
%   输出参数：
%     y     : 对应的 Range-Doppler 二维功率谱矩阵 [n_fft x n_bins]

    % 对 x 的每一列（即每个距离 bin 上的慢时间序列）应用窗函数乘积
    x_windowed = x .* w;
    
    % 沿慢时间维度（行，即维度 1）计算一维快速傅里叶变换 (FFT)
    rd_fft = fft(x_windowed, n_fft, 1);
    
    % 将零频分量平移至频谱中心
    rd_shifted = fftshift(rd_fft, 1);
    
    % 计算多普勒功率谱
    y = abs(rd_shifted).^2;
end
