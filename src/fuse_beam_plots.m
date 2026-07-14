function [fused_plots, stats] = fuse_beam_plots(all_raw_plots, fusion_params)
%FUSE_BEAM_PLOTS 多波位点迹三级融合。
%
% 输入：
%   all_raw_plots  : N×7 double, 列顺序 [r(m), az(deg), el(deg), vr(m/s), time(s), pwr(dB), beam_id]
%   fusion_params  : 结构体，字段：
%       .resolutions      : 1×4, [dr(m), daz(deg), del(deg), dvr(m/s)]
%       .dbscan_eps_grid  : DBSCAN 网格单元邻域半径（默认 3）
%       .dbscan_minpts_grid : DBSCAN 最小簇点数（默认 1）
% 输出：
%   fused_plots    : K×5 double, [r(m), az(deg), el(deg), vr(m/s), time(s)]
%   stats          : 结构体，包含各阶段点数统计
% 作用：
%   依次执行旁瓣鬼影抑制 → 邻域加权融合 → 网格 DBSCAN 三级融合。

if isempty(all_raw_plots)
    fused_plots = [];
    stats = struct('num_input', 0, 'num_ghosts_suppressed', 0, ...
        'num_after_fusion', 0, 'num_final', 0);
    return;
end

stats = struct();
stats.num_input = size(all_raw_plots, 1);
fprintf('\n========== 多波位点迹融合开始 ==========\n');
fprintf('[融合] 输入原始点迹数：%d\n', stats.num_input);

if numel(fusion_params.resolutions) < 4
    error('fuse_beam_plots:InvalidResolutions', ...
        'fusion_params.resolutions 必须包含 4 个元素：[dr, daz, del, dvr]');
end

% =====================================================================
% Stage A: 旁瓣鬼影抑制
% =====================================================================
% 同一 (R, V) 位置，功率差 > 6dB → 弱者为旁瓣鬼影，剔除。

SL_R_gate = fusion_params.resolutions(1) * 2.5;
SL_V_gate = fusion_params.resolutions(4) * 2.5;
SL_Thresh_dB = 6.0;

[~, sort_idx] = sort(all_raw_plots(:, 6), 'descend');
sorted_plots = all_raw_plots(sort_idx, :);

valid_mask = true(size(sorted_plots, 1), 1);
num_suppressed = 0;

for i = 1:size(sorted_plots, 1)
    if ~valid_mask(i), continue; end
    current = sorted_plots(i, :);
    for j = i + 1:size(sorted_plots, 1)
        if ~valid_mask(j), continue; end
        compare = sorted_plots(j, :);
        dR = abs(current(1) - compare(1));
        dV = abs(current(4) - compare(4));
        if dR <= SL_R_gate && dV <= SL_V_gate
            pwr_diff = current(6) - compare(6);
            if pwr_diff > SL_Thresh_dB
                valid_mask(j) = false;
                num_suppressed = num_suppressed + 1;
            end
        end
    end
end

plots_after_sl = sorted_plots(valid_mask, :);
stats.num_ghosts_suppressed = num_suppressed;
if num_suppressed > 0
    fprintf('[Stage A 旁瓣抑制] 剔除鬼影点数：%d（剩余 %d）\n', num_suppressed, size(plots_after_sl, 1));
else
    fprintf('[Stage A 旁瓣抑制] 未发现旁瓣鬼影\n');
end

% =====================================================================
% Stage B: 同源目标邻域加权融合
% =====================================================================
% 同一目标在波位交叠区可能被多个波位检出 → 4D 波门内功率加权平均。

r_gate  = fusion_params.resolutions(1) * 2.0;
v_gate  = fusion_params.resolutions(4) * 2.0;
az_gate = fusion_params.resolutions(2) * 3.0;
el_gate = fusion_params.resolutions(3) * 3.0;

plots_to_process = plots_after_sl;
plots_after_fusion = [];

while ~isempty(plots_to_process)
    seed = plots_to_process(1, :);
    dr_vec  = abs(plots_to_process(:, 1) - seed(1));
    dv_vec  = abs(plots_to_process(:, 4) - seed(4));
    daz_vec = abs(plots_to_process(:, 2) - seed(2));
    del_vec = abs(plots_to_process(:, 3) - seed(3));
    is_neighbor = (dr_vec <= r_gate) & (dv_vec <= v_gate) & ...
                  (daz_vec <= az_gate) & (del_vec <= el_gate);
    group = plots_to_process(is_neighbor, :);

    if size(group, 1) > 1
        pwr_lin = 10.^(group(:, 6) / 10);
        total_w = sum(pwr_lin);
        fused_r  = sum(group(:, 1) .* pwr_lin) / total_w;
        fused_az = sum(group(:, 2) .* pwr_lin) / total_w;
        fused_el = sum(group(:, 3) .* pwr_lin) / total_w;
        fused_vr = sum(group(:, 4) .* pwr_lin) / total_w;
        fused_time = group(1, 5);
        fused_pwr  = max(group(:, 6));
        fused_beam = group(1, 7);
        new_plot = [fused_r, fused_az, fused_el, fused_vr, fused_time, fused_pwr, fused_beam];
        plots_after_fusion = [plots_after_fusion; new_plot]; %#ok<AGROW>
    else
        plots_after_fusion = [plots_after_fusion; seed]; %#ok<AGROW>
    end
    plots_to_process(is_neighbor, :) = [];
end

stats.num_after_fusion = size(plots_after_fusion, 1);
fprintf('[Stage B 邻域融合] 融合后点迹数：%d\n', stats.num_after_fusion);

% =====================================================================
% Stage C: 网格 DBSCAN 最终聚类
% =====================================================================
eps_grid = 3;
minpts_grid = 1;
if isfield(fusion_params, 'dbscan_eps_grid') && ~isempty(fusion_params.dbscan_eps_grid)
    eps_grid = fusion_params.dbscan_eps_grid;
end
if isfield(fusion_params, 'dbscan_minpts_grid') && ~isempty(fusion_params.dbscan_minpts_grid)
    minpts_grid = fusion_params.dbscan_minpts_grid;
end

fused_plots = grid_dbscan_local(plots_after_fusion, fusion_params.resolutions, eps_grid, minpts_grid);

stats.num_final = size(fused_plots, 1);
fprintf('[Stage C 网格聚类] 最终量测数：%d\n', stats.num_final);
fprintf('========== 多波位点迹融合完成 ==========\n\n');
end

% =====================================================================
% 网格 DBSCAN 聚类（本地子函数）
% =====================================================================
function fused_plots = grid_dbscan_local(all_raw_plots, grid_resolutions, dbscan_eps, dbscan_minPts)
% 将物理坐标 [r, az, el, vr] 量化为网格单元后做 DBSCAN，
% 多点簇功率加权平均，孤立噪声点保留。

if isempty(all_raw_plots)
    fused_plots = [];
    return;
end

physical_data = all_raw_plots(:, 1:4);
power_dB = all_raw_plots(:, 6);
grid_res = grid_resolutions(:)';

if numel(grid_res) ~= 4
    error('grid_dbscan_local:InvalidResolutions', 'grid_resolutions 必须包含 4 个元素。');
end

quantized = round(physical_data ./ grid_res);

if size(all_raw_plots, 1) < dbscan_minPts
    fused_plots = all_raw_plots(:, 1:5);
    return;
end

try
    labels = dbscan(quantized, dbscan_eps, dbscan_minPts);
catch ME
    warning('grid_dbscan_local:DbscanFailed', ...
        'DBSCAN 失败（需 Statistics Toolbox）：%s，回退原始点集。', ME.message);
    fused_plots = all_raw_plots(:, 1:5);
    return;
end

fused_plots = zeros(0, 5);
unique_labels = unique(labels);

for ci = 1:numel(unique_labels)
    label = unique_labels(ci);
    idx = find(labels == label);

    if label == -1
        fused_plots = [fused_plots; all_raw_plots(idx, 1:5)]; %#ok<AGROW>
    else
        weights_lin = 10.^(power_dB(idx) / 10);
        total_w = sum(weights_lin);
        if total_w > 0
            fused_r  = sum(physical_data(idx, 1) .* weights_lin) / total_w;
            fused_az = sum(physical_data(idx, 2) .* weights_lin) / total_w;
            fused_el = sum(physical_data(idx, 3) .* weights_lin) / total_w;
            fused_vr = sum(physical_data(idx, 4) .* weights_lin) / total_w;
        else
            fused_r  = mean(physical_data(idx, 1));
            fused_az = mean(physical_data(idx, 2));
            fused_el = mean(physical_data(idx, 3));
            fused_vr = mean(physical_data(idx, 4));
        end
        fused_time = all_raw_plots(idx(1), 5);
        fused_plots = [fused_plots; fused_r, fused_az, fused_el, fused_vr, fused_time]; %#ok<AGROW>
    end
end

if ~isempty(fused_plots)
    [~, sort_idx] = sort(fused_plots(:, 1));
    fused_plots = fused_plots(sort_idx, :);
end
end
