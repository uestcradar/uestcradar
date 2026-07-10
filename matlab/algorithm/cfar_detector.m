function [det_map, target_r, target_v, peak_row, peak_col] = cfar_detector(...
    rd_power, r_axis, v_axis, valid_mask, r_disp_idx, guard_size, train_size, pfa)
% CFAR_DETECTOR 2D CA-CFAR (Cell-Averaging Constant False Alarm Rate) 二维恒虚警检测
%   [det_map, target_r, target_v, peak_row, peak_col] = cfar_detector(...)
%
%   输入参数：
%     rd_power   : 输入的 Range-Doppler 功率谱矩阵 [n_pulses x n_bins]
%     r_axis     : 完整的距离轴 (m)
%     v_axis     : 完整的速度轴 (m/s)
%     valid_mask : 有效检测区多普勒掩码（去除零频附近与超速区）
%     r_disp_idx : 限制进行检测和显示的距离 bin 索引范围
%     guard_size : 2D 保护单元单侧大小 [guard_r, guard_d] (分别对应距离和速度维)
%     train_size : 2D 训练单元单侧大小 [train_r, train_d] (分别对应距离和速度维)
%     pfa        : 虚警概率 (如 1e-4)
%
%   输出参数：
%     det_map    : 二维二进制检测矩阵 (1表示检测到目标，0表示背景/杂波)
%     target_r   : 检测区内能量最强的目标的物理距离 (m)
%     target_v   : 检测区内能量最强的目标的物理速度 (m/s)
%     peak_row   : 该最强目标在完整多普勒维的行索引
%     peak_col   : 该最强目标在完整距离维的列索引

    [num_rows, num_cols] = size(rd_power);
    det_map = zeros(num_rows, num_cols);

    gr = guard_size(1); gd = guard_size(2);
    tr = train_size(1); td = train_size(2);

    % 1. 计算训练单元数 N
    N = (2*tr + 2*gr + 1) * (2*td + 2*gd + 1) - (2*gr + 1) * (2*gd + 1);

    % 2. 计算门限乘积因子 alpha
    alpha = N * (pfa^(-1/N) - 1);

    % 3. 构造 2D CA-CFAR 卷积核
    kernel_total = ones(2*tr + 2*gr + 1, 2*td + 2*gd + 1);
    kernel_guard = zeros(2*tr + 2*gr + 1, 2*td + 2*gd + 1);
    kernel_guard(tr + 1 : tr + 2*gr + 1, td + 1 : td + 2*gd + 1) = 1;
    kernel_train = kernel_total - kernel_guard;

    % 4. 用 conv2 快速计算每个 CUT 周围训练单元的噪声功率之和
    noise_power_sum = conv2(rd_power, kernel_train, 'same');
    mean_noise_power = noise_power_sum / N;

    % 5. 比较门限进行检测
    threshold_mat = alpha * mean_noise_power;
    det_map = (rd_power > threshold_mat);

    % 6. 按原来流程：在显示距离范围和非零速度通道内，寻找能量最强的点
    %    （保证与原 GUI 绘制红圈和输出轨迹的逻辑向下兼容）
    rd_candidate = rd_power(valid_mask, r_disp_idx);
    [~, local_linear_idx] = max(rd_candidate(:));
    [local_v_row, local_r_col] = ind2sub(size(rd_candidate), local_linear_idx);

    nonzero_row_list = find(valid_mask);
    peak_row = nonzero_row_list(local_v_row);
    peak_col = r_disp_idx(local_r_col);

    target_v = v_axis(peak_row);
    target_r = r_axis(peak_col);
end
