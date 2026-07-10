function [r_m, v_m, az_m, el_m, sz_m] = mono_angle( ...
    r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
    pwr, az_ratio, el_ratio, k_mono, angle_r_range, angle_v_range, clim_lo)
%MONO_ANGLE 按聚类结果提取单脉冲测角结果。
%
% 输入：
%   r_disp     - 距离轴
%   v_disp     - 速度轴
%   det_r_idx  - 检测点距离索引
%   det_v_idx  - 检测点速度索引
%   clu_ids    - 聚类编号
%   n_clu      - 聚类数量
%   pwr        - 功率图
%   az_ratio   - 方位差通道比值图
%   el_ratio   - 俯仰差通道比值图
%   k_mono     - 单脉冲比例系数
%   angle_r_range - 保留的距离范围
%   angle_v_range - 保留的速度范围
%   clim_lo    - 绘图颜色下限
% 输出：
%   r_m, v_m, az_m, el_m, sz_m - 测角结果
% 作用：
%   从每个簇中选取代表点并计算方位、俯仰和显示尺寸。
%   单独保留这个模块的原因是：测角阶段的代表点选取规则、
%   单脉冲比例系数和显示尺寸策略都可能在后续实验中单独调整，
%   把它与检测、聚类解耦后，更便于独立调试和替换。

r_m = [];
v_m = [];
az_m = [];
el_m = [];
sz_m = [];

for ci = 1:n_clu
    idx_ci = find(clu_ids == ci);
    if isempty(idx_ci)
        continue;
    end

    ri = det_r_idx(idx_ci);
    vi = det_v_idx(idx_ci);
    pwr_ci = pwr(sub2ind(size(pwr), ri, vi));
    [~, best] = max(pwr_ci);
    rb = ri(best);
    vb = vi(best);

    r_val = r_disp(rb);
    v_val = v_disp(vb);
    if r_val < angle_r_range(1) || r_val > angle_r_range(2)
        continue;
    end
    if v_val < angle_v_range(1) || v_val > angle_v_range(2)
        continue;
    end

    r_m(end + 1) = r_val; %#ok<AGROW>
    v_m(end + 1) = v_val; %#ok<AGROW>
    az_m(end + 1) = az_ratio(rb, vb) / k_mono; %#ok<AGROW>
    el_m(end + 1) = el_ratio(rb, vb) / k_mono; %#ok<AGROW>
    sz_m(end + 1) = max(30, 20 * log10(pwr_ci(best) + eps) - clim_lo + 10); %#ok<AGROW>
end
end
