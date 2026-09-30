%% 无人机 RTK 真值 → 雷达相对坐标系 转换入口
% 独立于主数据处理链路：读 SRT → 坐标变换 → 存真值 mat + 画二维航迹图。
% 依赖: src/llh2radar_xyz.m (项目路径) + parseDjiSrt.m (参考代码, 在 rtkDir 下)。
% 二维航迹图画法与 src/plot_figures.m 的 plot_track_map_2d 一致：
%   X=距离向(阵面法向), Y=横向(正=阵面左侧), 虚线射线分隔各波位方位。
clear; clc; close all;

% ===================== 配置 =====================
rtkDir = 'F:\RTK';   % 文件选择对话框的起始目录

% 雷达原点 (RTK 实测, 椭球高)
radar_llh = [30.735683428, 103.912003013, 520.837];   % [纬度(°), 经度(°), 椭球高(m)]

% 阵面法向的罗盘朝向 (顺时针自正北, °)
radar_bearing_deg = 178.7;

% 波位方位角 (°)，用于画波位分割线（与当前 39 波位排布一致: 13 方位 × 3 俯仰）
beam_az_deg = -30:5:30;

% ===================== 手动选择并解析 SRT =====================
if exist(fullfile(rtkDir, 'parseDjiSrt.m'), 'file')
    addpath(rtkDir);
end

% 手动选择 SRT 文件（对话框默认定位到 rtkDir）
[srtFileName, srtPath] = uigetfile(fullfile(rtkDir, '*.SRT'), '选择无人机 SRT 真值文件');
if isequal(srtFileName, 0)
    error('未选择文件，已取消。');
end
srtFile = fullfile(srtPath, srtFileName);
if exist(fullfile(srtPath, 'parseDjiSrt.m'), 'file')
    addpath(srtPath);   % 若 parseDjiSrt 与被选 SRT 同目录，一并加入路径
end
fprintf('已选择 SRT: %s\n', srtFile);

drone = parseDjiSrt(srtFile);
if isempty(drone.time) || length(drone.time) < 2
    error('SRT 解析失败或有效点不足 2 个, 请检查文件格式。');
end

t   = drone.time;            % datetime (UTC, 绝对时间)
lat = drone.pos(:, 1);       % 纬度 (°)
lon = drone.pos(:, 2);       % 经度 (°)
h   = drone.pos(:, 3);       % 椭球高 (abs_alt, m)

% ===================== 坐标变换 =====================
uav_llh = [lat, lon, h];
xyz = llh2radar_xyz(uav_llh, radar_llh, radar_bearing_deg);
x = xyz(:, 1);               % 距离向 (阵面法向, 正前方)
y = xyz(:, 2);               % 横向 (正=阵面左侧; plot_figures 显示帧取 -y，故与其 Y 轴反号)
z = xyz(:, 3);               % 向上 (相对雷达椭球高)

% 极坐标 (备查/后续对比)
r  = sqrt(x.^2 + y.^2 + z.^2);
az = atan2(y, x);            % 方位 (rad)
el = atan2(z, sqrt(x.^2 + y.^2));   % 俯仰 (rad)

% 绝对时间转 Unix 秒, 供后续与雷达 param.t0 对齐
t_unix = posixtime(t);

% ===================== 保存真值 =====================
outMat = fullfile(rtkDir, 'uav_truth_radar.mat');
save(outMat, 't', 't_unix', 'lat', 'lon', 'h', ...
    'x', 'y', 'z', 'r', 'az', 'el', ...
    'radar_llh', 'radar_bearing_deg', 'beam_az_deg');
fprintf('真值已保存: %s\n', outMat);

% ===================== 打印诊断 =====================
fprintf('--- 真值诊断 ---\n');
fprintf('点数: %d\n', length(x));
fprintf('X 距离向范围: [%.1f, %.1f] m\n', min(x), max(x));
fprintf('Y 横向范围:   [%.1f, %.1f] m\n', min(y), max(y));
fprintf('Z 高度范围:   [%.1f, %.1f] m\n', min(z), max(z));
fprintf('斜距范围:     [%.1f, %.1f] m\n', min(r), max(r));
fprintf('阵面前方(X>0)占比: %.1f%%\n', 100 * mean(x > 0));

% ===================== 二维航迹图 (对齐原工程画法) =====================
R = 700;    % 射线/坐标轴长度, 与原工程 plot_track_map_2d 一致(已拓展到 700m)

fig = figure('Color', 'w', 'Name', '无人机真值-雷达相对坐标航迹');

% 雷达位置（原点）
h_radar = plot(0, 0, 'ks', 'MarkerSize', 10, 'MarkerFaceColor', 'k');
hold on;

% 波位方位分割线（虚线，去重，与原工程一致: 正方位=左侧 → Y负半轴）
h_ray = gobjects(1, numel(beam_az_deg));
for b = 1:numel(beam_az_deg)
    th = -beam_az_deg(b);   % 显示角（正方位=左侧 → Y负半轴）
    h_ray(b) = plot([0, R * cosd(th)], [0, R * sind(th)], ...
        '--', 'Color', [0.60 0.60 0.60], 'LineWidth', 0.8);
end

% 真值航迹线 + 起终点
h_track = plot(x, y, '-', 'Color', [0.20 0.45 0.90], 'LineWidth', 1.4);
h_start = plot(x(1),   y(1),   'go', 'MarkerFaceColor', 'g', 'MarkerSize', 8);   % 起点
h_end   = plot(x(end), y(end), 'ro', 'MarkerFaceColor', 'r', 'MarkerSize', 8);   % 终点

hold off;
axis equal;
xlim([0, R]);
ylim([-300, 300]);
xlabel('X (m)');
ylabel('Y (m)');
grid on;
box on;
title(sprintf('无人机真值航迹 (雷达相对坐标, 阵面朝向 %d°)', radar_bearing_deg));
legend([h_radar, h_ray(1), h_track, h_start, h_end], ...
    {'雷达', '波位分割线', '航迹', '起点', '终点'}, 'Location', 'best');

% 保存 PNG（保留交互图窗口）
png_file = fullfile(rtkDir, sprintf('UAV_Truth_2D_%s.png', ...
    char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'))));
saveas(fig, png_file);
fprintf('航迹图已保存: %s\n', png_file);
