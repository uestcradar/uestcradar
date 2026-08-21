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

            if isfield(tr, 'updated_mask') && numel(tr.updated_mask) == size(path, 1)
                up = logical(tr.updated_mask);
            elseif isfield(tr, 'meas_path') && size(tr.meas_path, 1) == size(path, 1)
                up = ~any(isnan(tr.meas_path), 2);
            else
                up = true(size(path, 1), 1);
            end
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


