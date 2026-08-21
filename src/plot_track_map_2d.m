function plot_track_map_2d(tracks, beam_schedule, result_dir)
%PLOT_TRACK_MAP_2D 二维航迹图（笛卡尔地面投影，静态点线图），每条航迹逐点标记并连线。
% 输入：
%   tracks        : 跟踪器输出的航迹结构体数组（含 path: L×3 笛卡尔 [x,y,z]）
%   beam_schedule : 波位排布（用于画波位方位虚线）
%   result_dir    : 输出目录
% 作用：
%   X=x(m), Y=y(m) 笛卡尔地面投影（原点=雷达）。path 已是笛卡尔，直接取第1、2列。
%   画已确认航迹的滤波轨迹线（全部帧，含预测帧），并在「有量测更新」的帧标出航点；
%   不同航迹不同颜色；每个唯一波位方位角画一条从原点出发的虚线射线。

if isempty(tracks)
    fprintf('[航迹图] 无航迹，跳过。\n');
    return;
end

% 只保留「已确认」且「至少 2 帧」的航迹
is_confirmed = [tracks.is_confirmed];
has_pts = arrayfun(@(t) size(t.path, 1) >= 2, tracks);
tracks_plot = tracks(is_confirmed & has_pts);
if isempty(tracks_plot)
    fprintf('[航迹图] 无已确认航迹（或量测更新点不足），跳过。\n');
    return;
end

fig = figure('Visible', 'off', 'Position', [100, 100, 900, 900]);

% 雷达位置（原点）
plot(0, 0, 'ks', 'MarkerSize', 10, 'MarkerFaceColor', 'k');
hold on;

% 波位方位虚线射线（去重：多个波位可能同方位不同俯仰）
% 波位方位角为前端约定（正=左侧），显示帧 Y+=右侧，故射线取负对齐。
uniq_az = unique(beam_schedule.beam_positions(:, 1));
for b = 1:numel(uniq_az)
    th = -uniq_az(b);
    plot([0, 600 * cosd(th)], [0, 600 * sind(th)], ...
        '--', 'Color', [0.60 0.60 0.60], 'LineWidth', 0.8);
end

% 逐条航迹：滤波轨迹线（全部帧），有量测更新帧标出航点
cmap = lines(max(numel(tracks_plot), 1));
for t = 1:numel(tracks_plot)
    tr = tracks_plot(t);
    path = tr.path;                          % 滤波轨迹（含预测帧，无 NaN）
    c = cmap(mod(t - 1, size(cmap, 1)) + 1, :);

    plot(path(:, 1), -path(:, 2), '-', 'Color', c, 'LineWidth', 1.4);

    % 有量测更新的帧标出航点（旧数据无 updated_mask 时回退用 meas_path 的 NaN 判定）
    if isfield(tr, 'updated_mask') && numel(tr.updated_mask) == size(path, 1)
        up = logical(tr.updated_mask);
    elseif isfield(tr, 'meas_path') && size(tr.meas_path, 1) == size(path, 1)
        up = ~any(isnan(tr.meas_path), 2);
    else
        up = true(size(path, 1), 1);
    end
    plot(path(up, 1), -path(up, 2), 'o', 'Color', c, ...
        'MarkerSize', 4, 'MarkerFaceColor', c, 'MarkerEdgeColor', c);
end

hold off;

axis equal;
xlim([0, 600]);
ylim([-300, 300]);
xlabel('X (m)');
ylabel('Y (m)');
grid on;
box on;
title(sprintf('二维航迹图：%d 条航迹', numel(tracks_plot)));

png_file = fullfile(result_dir, sprintf('Track_Map_2D_%s.png', ...
    char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'))));
saveas(fig, png_file);
close(fig);
fprintf('[航迹图] 已保存：%s\n', png_file);
end
