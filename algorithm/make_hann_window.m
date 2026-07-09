function w = make_hann_window(n)
% MAKE_HANN_WINDOW 生成 Hann 慢时间窗，避免依赖特定工具箱函数。
%   w = make_hann_window(n)
%   返回单精度列向量，便于直接与 [pulse, range_bin] 矩阵逐行相乘。

    if n <= 1
        w = single(1);
        return;
    end

    idx = single((0:n-1).');
    w = single(0.5) - single(0.5) * cos(single(2 * pi) * idx / single(n - 1));
end
