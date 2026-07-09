function y = mti_two_pulse(x)
% MTI_TWO_PULSE 慢时间双脉冲延迟对消 MTI 滤波器 (H(z) = 1 - z^-1)
%   y = mti_two_pulse(x)
%
%   输入参数：
%     x : 输入的二维雷达信号矩阵（行数为脉冲/慢时间，列数为距离/快时间）
%
%   输出参数：
%     y : 对消后的二维雷达信号矩阵 (首行进行零填充以保持原矩阵维度不变)

    [n_pulses, n_bins] = size(x);
    
    if n_pulses < 2
        y = x;
        return;
    end
    
    % 对消滤波（当前脉冲减去前一脉冲）
    y_diff = diff(x, 1, 1);
    
    % 头部填补一行零，使输出维度与输入 x 完全一致，便于后续 Doppler FFT
    y = [zeros(1, n_bins); y_diff];
end
