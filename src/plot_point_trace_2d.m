function plot_point_trace_2d(fused_plots, beam_schedule, result_dir)
%PLOT_POINT_TRACE_2D 二维点迹图（笛卡尔地面投影），聚合所有时刻目标，虚线分隔波位。
% 输入：
%   fused_plots   : K×6 [r(m), az(deg), el(deg), vr(m/s), time(s), scan_id]
%   beam_schedule : 波位排布（含 beam_positions = [az, el]）
%   result_dir    : 输出目录
% 作用：
%   X=x(m), Y=y(m) 笛卡尔地面投影（原点=雷达）。球坐标转笛卡尔：
%   x=r·cos(el)·cos(az), y=-r·cos(el)·sin(az)（正 az=左侧）。
%   每个唯一波位方位角画一条从原点出发的虚线射线，隔开各波位。

if isempty(fused_plots)
    fprintf('[点迹图] 无融合目标，跳过。\n');
    return;
end

r  = fused_plots(:, 1);
az = fused_plots(:, 2);
el = fused_plots(:, 3);
x = r .* cosd(el) .* cosd(az);
y = -r .* cosd(el) .* sind(az);   % 正方位角=左侧 → Y负半轴（Y+=右侧）

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

% 目标点
scatter(x, y, 24, 'b', 'filled');

hold off;
axis equal;
xlim([0, 600]);
ylim([-300, 300]);
xlabel('X (m)');
ylabel('Y (m)');
grid on;
box on;
title(sprintf('二维点迹图：全时刻 %d 个目标，%d 个波位方位', ...
    size(fused_plots, 1), numel(uniq_az)));

png_file = fullfile(result_dir, sprintf('Point_Trace_2D_%s.png', ...
    char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'))));
saveas(fig, png_file);
close(fig);
fprintf('[点迹图] 已保存：%s\n', png_file);
end


