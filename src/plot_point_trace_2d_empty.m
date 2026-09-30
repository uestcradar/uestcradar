%PLOT_POINT_TRACE_2D_EMPTY 波位示意图（方位向 + 俯仰向）：直接运行，展示波位排布。
% 作用：画出与 plot_figures.m 内 point_trace_2d 同风格的坐标系底图（雷达原点、虚线射线、坐标轴），
%       不画目标点。全部波位画灰色虚线分割线；受静态杂波干扰而弃用的波位，
%       在其分割线上方叠半透明蓝色扇形区域（分割线仍可见）。
% 注意：正式的 plot_figures.m 用的是**本批数据实际解析出的** beam_schedule.beam_positions，
%       本脚本用的是下面写死的 39 波位排布 —— 两者在换批次后可能不一致，仅作示意图。
% 用法：直接运行本脚本（F5 / Run），无需传参。

% 0. 环境：把 src 加入路径（与批处理管线保持一致）
this_dir = fileparts(mfilename('fullpath'));
project_root = fileparts(this_dir);
addpath(project_root);
addpath(genpath(fullfile(project_root, 'src')));

% 1. 波位角（单位：°，与前端约定一致：正方位=右侧，正俯仰=向上）
%    全波位：39 个 = 方位 -30~30°（步进5°，13 个）× 俯仰 0/5/10°（3 个），
%    与 build_beam_schedule_from_meta.m 从帧内嵌元数据得到的排布一致。
%    若某批数据中有受静态杂波干扰而弃用的波位，把范围填进下面两个变量即可叠蓝区；
%    当前 39 波位排布下没有已确认的弃用范围，故留空。
az_all     = -30:5:30;                   % 全部方位（分割线）
el_all     = [0 5 10];                   % 全部俯仰（分割线）
az_discard = {};                         % 弃用方位范围（az），如 {[-30 -10], [25 30]}
el_discard = [];                         % 弃用俯仰范围（el），如 [0 5]

R = 700;                       % 射线/扇形半径（与底图一致，已拓展到 700m）
blue = [0.25 0.45 0.85];       % 弃用区域颜色（蓝）
alpha_discard = 0.3;           % 弃用区域透明度（0=全透明，1=不透明）

% ---- 图 1：方位向波位示意图（X-Y 地面投影） ----
figure('Position', [100, 100, 700, 700]);
hold on;

% 全部方位分割线（虚线，含弃用波位）
for b = 1:numel(az_all)
    th = -az_all(b);                        % 显示角（正方位=右侧 → Y负半轴）
    plot([0, R * cosd(th)], [0, R * sind(th)], ...
        '--', 'Color', [0.60 0.60 0.60], 'LineWidth', 0.8);
end

% 弃用区域（az_discard 非空时）：叠半透明蓝色扇形（分割线仍可见）
for i = 1:numel(az_discard)
    th  = -az_discard{i};
    ang = linspace(th(1), th(2), 60);
    fill([0, R * cosd(ang), 0], [0, R * sind(ang), 0], blue, ...
        'EdgeColor', 'none', 'FaceAlpha', alpha_discard);
end

% 雷达位置（原点）
plot(0, 0, 'ks', 'MarkerSize', 10, 'MarkerFaceColor', 'k');

hold off;
axis equal;
xlim([0, 700]);
ylim([-300, 300]);
xlabel('X (m)');
ylabel('Y (m)');
grid on;
box on;
title('波位示意图（方位向）', 'FontName', 'Microsoft YaHei');

% ---- 图 2：俯仰向波位示意图（Y-Z 剖面） ----
figure('Position', [850, 100, 700, 420]);
hold on;

% 全部俯仰分割线（虚线，含弃用波位）
for b = 1:numel(el_all)
    el = el_all(b);
    plot([0, R * cosd(el)], [0, R * sind(el)], ...
        '--', 'Color', [0.60 0.60 0.60], 'LineWidth', 0.8);
end

% 弃用区域（el_discard 非空时）：叠半透明蓝色扇形
if ~isempty(el_discard)
    ang = linspace(el_discard(1), el_discard(2), 60);
    fill([0, R * cosd(ang), 0], [0, R * sind(ang), 0], blue, ...
        'EdgeColor', 'none', 'FaceAlpha', alpha_discard);
end

% 雷达位置（原点）
plot(0, 0, 'ks', 'MarkerSize', 10, 'MarkerFaceColor', 'k');

hold off;
axis equal;
xlim([0, 700]);
% 俯仰均为向上，高度只取地面以上（正值）
hmax = R * sind(max(el_all)) + 20;
ylim([0, hmax]);
xlabel('Y (m)');
ylabel('Z (m)');
grid on;
box on;
title('波位示意图（俯仰向）', 'FontName', 'Microsoft YaHei');
