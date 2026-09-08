function plot_figures(kind, varargin)
%PLOT_FIGURES 绘图统一入口：按 kind 分发到对应的画图本地函数。
%
% kind 取值与对应函数：
%   'beam_timeline_gif'  逐帧融合后目标动图（RD 时间线 GIF）
%   'raw_rd_gif'         逐波位原始 RD 热力图 GIF（不经 CFAR，调试用）
%   'point_trace_2d'     二维点迹图（笛卡尔地面投影）
%   'track_map_2d'       二维航迹图（笛卡尔地面投影）
%   'tracking_3d_gif'    三维航迹动图
%
% 用法示例：
%   plot_figures('point_trace_2d', fused_plots, beam_schedule, result_dir);

switch kind
    case 'beam_timeline_gif'
        plot_beam_timeline_gif(varargin{:});
    case 'raw_rd_gif'
        save_raw_rd_gif_local(varargin{:});
    case 'point_trace_2d'
        plot_point_trace_2d(varargin{:});
    case 'track_map_2d'
        plot_track_map_2d(varargin{:});
    case 'tracking_3d_gif'
        plot_tracking_3d_gif(varargin{:});
    otherwise
        error('plot_figures:UnknownKind', '未知绘图类型: %s', kind);
end
end


function plot_beam_timeline_gif(all_raw_plots, fused_plots, total_frames, result_dir, cfg, track_results)
%PLOT_BEAM_TIMELINE_GIF 逐帧融合后目标动图，叠加跟踪航迹。
% 按 scan_id = 1..total_frames 全部遍历，无目标的帧显示空画面。
% fused_plots 列: [r(m), az(deg), el(deg), vr(m/s), time(s), scan_id]
% track_results: cell array，每帧对应的 tracks 结构体数组。

if total_frames < 1
    fprintf('[时间线GIF] 无有效帧，跳过。\n');
    return;
end

use_fused = ~isempty(fused_plots) && size(fused_plots, 2) >= 6;
has_tracks = ~isempty(track_results);

track_msg = '';
if has_tracks, track_msg = ' + 航迹'; end
fprintf('[时间线GIF] 生成 %d 帧动图（融合后目标%s）...\n', total_frames, track_msg);

fig = figure('Visible', 'off', 'Position', [100, 100, 800, 600]);
gif_file = fullfile(result_dir, sprintf('Timeline_%s.gif', ...
    char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'))));

delay = cfg.export.gif_delay;
r_range = cfg.plot.range_window_m;
v_range = cfg.plot.velocity_window_mps;

gif_colormap = [];  % 首帧建立，后续复用，避免调色板漂移导致 GIF 截断

for fi = 1:cfg.plot.frame_step:total_frames
    clf;
    sid = fi;

    % 当前帧的量测数据
    r_data = []; v_data = []; marker_sz = [];

    if use_fused
        mask = fused_plots(:, 6) == sid;
        if any(mask)
            frame = fused_plots(mask, :);
            r_data = frame(:, 1);
            v_data = frame(:, 4);
            marker_sz = 50 * ones(size(r_data));
        end
    elseif ~isempty(all_raw_plots)
        mask = all_raw_plots(:, 8) == sid;
        if any(mask)
            frame = all_raw_plots(mask, :);
            r_data = frame(:, 1);
            v_data = frame(:, 4);
            pwr_data = frame(:, 6);
            marker_sz = max(15, pwr_data - min(pwr_data) + 15);
        end
    end

    % 量测散点（无目标时仍显示空坐标轴）
    if ~isempty(r_data)
        scatter(v_data, r_data, marker_sz, 'b', 'filled');
    end

    xlim(v_range);
    ylim(r_range);
    xlabel('Radial Velocity (m/s)');
    ylabel('Range (m)');

    n_meas = numel(r_data);
    title_str = sprintf('Scan %d/%d  N=%d', fi, total_frames, n_meas);
    title(title_str);
    grid on;
    box on;

    drawnow;

    frame_img = getframe(fig);
    im = frame2im(frame_img);
    if isempty(gif_colormap)
        [A, gif_colormap] = rgb2ind(im, 256, 'nodither');
        imwrite(A, gif_colormap, gif_file, 'gif', 'LoopCount', inf, 'DelayTime', delay);
    else
        A = rgb2ind(im, gif_colormap, 'nodither');
        imwrite(A, gif_colormap, gif_file, 'gif', 'WriteMode', 'append', 'DelayTime', delay);
    end

    if mod(fi, 100) == 0
        fprintf('[时间线GIF] %d/%d 帧已写入 (%.0f%%)\n', fi, total_frames, fi/total_frames*100);
    end
end

close(fig);
fprintf('[时间线GIF] 已保存：%s\n', gif_file);
end


function save_raw_rd_gif_local(rd_file, beam_id, beam_az, beam_el, cfg)
%SAVE_RAW_RD_GIF_LOCAL 从 RD 文件生成原始热力图 GIF（不经 CFAR），保存在 RD 文件同目录。
rd = matfile(rd_file);
r_axis = builtin('double', rd.r_axis_full);
v_axis = builtin('double', rd.v_axis_full);
n_blocks = builtin('double', rd.total_blocks);

r_idx = find(r_axis >= cfg.plot.range_window_m(1) & r_axis <= cfg.plot.range_window_m(2));
v_idx = find(v_axis >= cfg.plot.velocity_window_mps(1) & v_axis <= cfg.plot.velocity_window_mps(2));
r_disp = r_axis(r_idx);
v_disp = v_axis(v_idx);

[fp, fn] = fileparts(rd_file);
gif_file = fullfile(fp, sprintf('%s_raw_rd.gif', fn));

fig = figure('Visible', 'off', 'Position', [100, 100, 800, 600]);
gif_colormap = [];

frame_ids = 1:cfg.plot.frame_step:n_blocks;
for fi = 1:numel(frame_ids)
    k = frame_ids(fi);
    clf;
    rd_frame = abs(rd.RD_Sum_All(r_idx, v_idx, k));
    rd_dB = 20 * log10(rd_frame + eps);

    imagesc(v_disp, r_disp, rd_dB);
    set(gca, 'YDir', 'normal');
    caxis(cfg.plot.clim_dB);
    colormap(jet(256));
    colorbar;
    xlabel('Velocity (m/s)');
    ylabel('Range (m)');
    title(sprintf('Beam %d (az=%.1f\\circ el=%.1f\\circ)  Raw RD  %d/%d', ...
        beam_id, beam_az, beam_el, k, n_blocks));
    axis xy;

    drawnow;
    frame_img = getframe(fig);
    im = frame2im(frame_img);
    if isempty(gif_colormap)
        [A, gif_colormap] = rgb2ind(im, 256, 'nodither');
        imwrite(A, gif_colormap, gif_file, 'gif', 'LoopCount', inf, 'DelayTime', cfg.export.gif_delay);
    else
        A = rgb2ind(im, gif_colormap, 'nodither');
        imwrite(A, gif_colormap, gif_file, 'gif', 'WriteMode', 'append', 'DelayTime', cfg.export.gif_delay);
    end
end

close(fig);
cfg.runtime.status_cb(sprintf('[RawRD] 波位 %d (az=%.1f°) 原始 RD GIF 已保存: %s', ...
    beam_id, beam_az, gif_file));
end


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
    up = measurement_mask(tr);
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


function plot_tracking_3d_gif(fused_plots, total_frames, track_results, radar_height, result_dir, frame_step, gif_delay)
%PLOT_TRACKING_3D_GIF 三维航迹动图。
% 显示雷达位置、融合量测、已确认航迹（滤波轨迹线，量测更新帧标航点，蓝色）。
%
% 输入：
%   fused_plots  : K×6 [r(m), az(deg), el(deg), vr(m/s), time(s), scan_id]
%   total_frames : 总扫描帧数
%   track_results: cell array，每帧对应的 tracks 结构体数组
%   radar_height : 雷达架高 (m)
%   result_dir   : 输出目录
%   frame_step   : GIF 抽帧步长
%   gif_delay    : GIF 帧间延时 (s)

if isempty(track_results)
    fprintf('[3D航迹] 无跟踪数据，跳过。\n');
    return;
end

fprintf('[3D航迹] 生成 %d 帧 3D 动图...\n', total_frames);

fig = figure('Visible', 'off', 'Position', [100, 100, 1000, 600]);
gif_file = fullfile(result_dir, sprintf('Tracking_3D_%s.gif', ...
    char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'))));

delay = gif_delay;

gif_colormap = [];

% 收集已确认航迹 ID 并重新编号 → 显示 T1, T2, ...
all_confirmed_ids = [];
for fi = 1:total_frames
    tf = track_results{fi};
    if ~isempty(tf) && any([tf.is_confirmed])
        all_confirmed_ids = [all_confirmed_ids, tf([tf.is_confirmed]).track_id]; %#ok<AGROW>
    end
end
all_confirmed_ids = unique(all_confirmed_ids, 'stable');
id_to_disp = zeros(1, max(all_confirmed_ids));
for di = 1:numel(all_confirmed_ids)
    id_to_disp(all_confirmed_ids(di)) = di;
end

for fi = 1:frame_step:total_frames
    clf;
    hold on;
    grid on;
    axis equal;

    % --- 图例收集 ---
    legend_handles = [];
    legend_labels = {};
    has_meas = false;
    has_track = false;

    % 雷达位置（黑色三角参考点，非数据点）
    h = plot3(0, 0, radar_height, 'k^', 'MarkerSize', 10, 'MarkerFaceColor', 'k');
    legend_handles(end+1) = h;
    legend_labels{end+1} = 'Radar';

    % 当前帧量测：球坐标 → 笛卡尔（蓝色小点）
    if ~isempty(fused_plots)
        mask = fused_plots(:, 6) == fi;
        if any(mask)
            r  = fused_plots(mask, 1);
            az = fused_plots(mask, 2);
            el = fused_plots(mask, 3);
            x = r .* cosd(el) .* cosd(az);
            y = -r .* cosd(el) .* sind(az);   % 正方位角=左侧 → Y负半轴（Y+=右侧）
            z = r .* sind(el) + radar_height;
            h = plot3(x, y, z, 'b.', 'MarkerSize', 8);
            if ~has_meas
                legend_handles(end+1) = h;
                legend_labels{end+1} = 'Measurements';
                has_meas = true;
            end
        end
    end

    % 航迹（蓝色点线，只连匹配量测点，不画预测点）
    tracks_frame = track_results{fi};
    n_track = 0;
    if ~isempty(tracks_frame)
        for ti = 1:numel(tracks_frame)
            tr = tracks_frame(ti);
            if ~tr.is_confirmed
                continue;  % 未确认航迹（噪点）不显示
            end

            % 滤波轨迹线（全部帧），有量测更新帧标出航点
            path = tr.path;   % 滤波轨迹（含预测帧，无 NaN）
            if size(path, 1) < 2, continue; end

            h_path = plot3(path(:, 1), -path(:, 2), path(:, 3), 'b-', 'LineWidth', 1.5);

            up = measurement_mask(tr);
            plot3(path(up, 1), -path(up, 2), path(up, 3), 'bo', ...
                'MarkerSize', 4, 'MarkerFaceColor', 'b', 'MarkerEdgeColor', 'b');

            plot3(path(end, 1), -path(end, 2), path(end, 3), 'bo', 'MarkerSize', 6, 'MarkerFaceColor', 'b');
            text(path(end, 1) + 50, -path(end, 2), path(end, 3) + 20, ...
                sprintf('T%d', id_to_disp(tr.track_id)), 'Color', 'b', 'FontWeight', 'bold', 'FontSize', 12);
            if ~has_track
                legend_handles(end+1) = h_path;
                legend_labels{end+1} = 'Confirmed Track';
                has_track = true;
            end
            n_track = n_track + 1;
        end
    end

    hold off;

    xlim([0, 600]);
    ylim([-300, 300]);
    zlim([0, 200]);
    xlabel('X (m)');
    ylabel('Y (m)');
    zlabel('Z (m)');
    view(-35, 25);
    title(sprintf('3D Tracking  Frame %d/%d  Confirmed:%d', fi, total_frames, n_track));

    % 图例
    legend(gca, legend_handles, legend_labels, 'Location', 'northwest');

    drawnow;

    frame_img = getframe(fig);
    im = frame2im(frame_img);
    if isempty(gif_colormap)
        [A, gif_colormap] = rgb2ind(im, 256, 'nodither');
        imwrite(A, gif_colormap, gif_file, 'gif', 'LoopCount', inf, 'DelayTime', delay);
    else
        A = rgb2ind(im, gif_colormap, 'nodither');
        imwrite(A, gif_colormap, gif_file, 'gif', 'WriteMode', 'append', 'DelayTime', delay);
    end

    if mod(fi, 100) == 0
        fprintf('[3D航迹] %d/%d 帧已写入 (%.0f%%)\n', fi, total_frames, fi/total_frames*100);
    end
end

close(fig);
fprintf('[3D航迹] 已保存：%s\n', gif_file);
end

function up = measurement_mask(tr)
%MEASUREMENT_MASK 返回航迹各帧是否关联到真实量测的逻辑掩码。
% 优先用 updated_mask；旧数据缺该字段时回退用 meas_path 的 NaN 判定。
path = tr.path;
if isfield(tr, 'updated_mask') && numel(tr.updated_mask) == size(path, 1)
    up = logical(tr.updated_mask);
elseif isfield(tr, 'meas_path') && size(tr.meas_path, 1) == size(path, 1)
    up = ~any(isnan(tr.meas_path), 2);
else
    up = true(size(path, 1), 1);
end
end
