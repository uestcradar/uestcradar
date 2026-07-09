function y = pulse_compression(x, h, ref_len, pri_len)
% PULSE_COMPRESSION 雷达脉冲压缩（匹配滤波，保留完整卷积结果）
%   y = pulse_compression(x, h, ref_len, pri_len)
%
%   输入参数：
%     x       : 输入的二维雷达信号矩阵（行数为脉冲/慢时间，列数为距离/快时间）
%     h       : 匹配滤波器系数（通常为共轭时反参考脉冲，列向量） [ref_len x 1]
%     ref_len : 发射参考信号的有效点数
%     pri_len : 雷达的 PRI 采样点数
%
%   输出参数：
%     y       : 脉压后的二维雷达信号矩阵 [coherentPri x (pri_len + ref_len - 1)]

    % 沿快时间维度进行一维卷积 (二维 conv2 时 h.' 为行向量，对 x 的每一行进行一维卷积)
    y = conv2(x, h.', 'full');
end
