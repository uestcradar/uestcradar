function y = mti_avg(x, alpha)
% MTI_AVG 慢时间平均对消 MTI 滤波器（消除直流/静止分量）
%   y = mti_avg(x, alpha)
%
%   输入参数：
%     x     : 输入的二维雷达信号矩阵（行数为脉冲/慢时间，列数为距离/快时间）
%     alpha : MTI 抑制强度系数 (通常在 0.9 ~ 0.99 之间，如 0.95)
%
%   输出参数：
%     y     : 滤波后的二维雷达信号矩阵

    n_pulses = size(x, 1);
    
    % 计算每个距离 bin 在慢时间上的直流均值
    clutter_mean = mean(x, 1);
    
    % 减去直流均值的一部分（若 alpha = 1.0 则为完全静止杂波对消）
    y = x - alpha * repmat(clutter_mean, n_pulses, 1);
end
