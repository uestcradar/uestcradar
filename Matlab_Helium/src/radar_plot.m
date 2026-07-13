function out = radar_plot(mat_path, result_dir, plot_cfg, det_result, ang_result)
%RADAR_PLOT 纯绘图模块。
%
% 输入：
%   mat_path    - RD_Proc_*.mat 文件或所在目录
%   result_dir  - 结果输出目录
%   plot_cfg    - 绘图参数，需包含 status_cb
%   det_result  - 目标检测结果结构体，可为空
%   ang_result  - 测角结果结构体，可为空
% 输出：
%   out         - 输出文件路径摘要
% 作用：
%   只负责把已经准备好的 RD、检测和测角结果画成 GIF。
%   单独保留这个模块的原因是：绘图属于结果消费层，
%   与前面的解析、RD、检测和测角解耦后，后续调整显示风格时不会影响算法主链路。

if nargin < 1 || isempty(mat_path)
    error('radar_plot:MissingInput', '请输入 RD_Proc_*.mat 文件路径或其所在目录。');
end
if isfolder(mat_path)
    fl = dir(fullfile(mat_path, 'RD_Proc_*.mat'));
    if isempty(fl)
        error('radar_plot:MissingResultFile', '未在目录中找到 RD_Proc_*.mat：%s', mat_path);
    end
    [~, ii] = max([fl.datenum]);
    mat_path = fullfile(mat_path, fl(ii).name);
end
if nargin < 2 || isempty(result_dir)
    error('radar_plot:MissingOutputDir', '请提供结果输出目录。');
end
if ~exist(result_dir, 'dir')
    mkdir(result_dir);
end
if nargin < 3 || isempty(plot_cfg)
    error('radar_plot:MissingOpts', '请提供绘图参数结构体。');
end
if nargin < 4
    det_result = [];
end
if nargin < 5
    ang_result = [];
end

r_range = plot_cfg.r_range;
v_range = plot_cfg.v_range;
clim_dB = plot_cfg.clim_dB;
frame_step = plot_cfg.frame_step;
delay = plot_cfg.delay;
status_cb = plot_cfg.status_cb;
k_mono = plot_cfg.k_mono;

lg = status_cb;
lg(sprintf('[绘图] 读取 RD 文件：%s', mat_path));

mf = matfile(mat_path);
r_axis = double(mf.r_axis_full);
v_axis = double(mf.v_axis_full);
n_frames = double(mf.total_blocks);

r_mask = r_axis >= r_range(1) & r_axis <= r_range(2);
v_mask = v_axis >= v_range(1) & v_axis <= v_range(2);
r_disp = r_axis(r_mask);
v_disp = v_axis(v_mask);
frame_ids = 1:frame_step:n_frames;
nf = numel(frame_ids);

[~, ds] = fileparts(mat_path);
ts = char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'));
gif_rd = fullfile(result_dir, sprintf('%s_%s_rd.gif', ts, ds));
gif_det = fullfile(result_dir, sprintf('%s_%s_det.gif', ts, ds));
gif_ang = fullfile(result_dir, sprintf('%s_%s_angle.gif', ts, ds));

%% 1) RD GIF
lg(sprintf('[绘图 1/3] 导出 RD 动图：%s', gif_rd));
fig_rd = new_fig(950, 650);
ax_rd = axes('Parent', fig_rd);
rd0_init = amplitude_to_db(mf.RD_Sum_All(:, :, frame_ids(1)), r_mask, v_mask);
h_img_rd = imagesc(ax_rd, v_disp, r_disp, rd0_init, [clim_dB(1), clim_dB(2)]);
set(ax_rd, 'YDir', 'normal', 'FontSize', 12, 'FontName', 'Microsoft YaHei');
colormap(ax_rd, jet(256));
cb_rd = colorbar(ax_rd);
cb_rd.Label.String = 'Amplitude (dB)';
cb_rd.Label.FontName = 'Microsoft YaHei';
cb_rd.FontSize = 11;
xlabel(ax_rd, 'Radial velocity (m/s)', 'FontSize', 13, 'FontName', 'Microsoft YaHei');
ylabel(ax_rd, 'Range (m)', 'FontSize', 13, 'FontName', 'Microsoft YaHei');
h_tit_rd = title(ax_rd, '', 'FontSize', 11, 'Interpreter', 'none', 'FontName', 'Microsoft YaHei');

gif_cm_rd = [];
for frame_idx = 1:nf
    k = frame_ids(frame_idx);
    h_img_rd.CData = amplitude_to_db(mf.RD_Sum_All(:, :, k), r_mask, v_mask);
    h_tit_rd.String = sprintf('RD 第 %d/%d 帧 | %s', frame_ids(frame_idx), n_frames, ds);
    gif_cm_rd = write_gif_frame(fig_rd, gif_rd, frame_idx, delay, gif_cm_rd);
    if mod(frame_idx, 200) == 0 || frame_idx == nf
        lg(sprintf('  [绘图] RD 已完成 %d/%d 帧', frame_idx, nf));
    end
end
close(fig_rd);

%% 2) Detection GIF
if ~isempty(det_result)
    lg(sprintf('[绘图 2/3] 导出检测动图：%s', gif_det));
    fig_det = new_fig(800, 600);
    set(fig_det, 'Color', 'k');
    ax_det = axes('Parent', fig_det, ...
        'Color', 'k', 'XColor', 'w', 'YColor', 'w', ...
        'FontSize', 12, 'FontName', 'Microsoft YaHei', ...
        'GridColor', [0.3 0.3 0.3], 'GridAlpha', 0.5);
    xlim(ax_det, v_range);
    ylim(ax_det, r_range);
    set(ax_det, 'YDir', 'normal');
    grid(ax_det, 'on');
    box(ax_det, 'on');
    xlabel(ax_det, 'Radial velocity (m/s)', 'FontSize', 13, 'Color', 'w', 'FontName', 'Microsoft YaHei');
    ylabel(ax_det, 'Range (m)', 'FontSize', 13, 'Color', 'w', 'FontName', 'Microsoft YaHei');
    h_tit_det = title(ax_det, '', 'FontSize', 11, 'Interpreter', 'none', ...
        'Color', 'w', 'FontName', 'Microsoft YaHei');

    gif_cm_det = [];
    for frame_idx = 1:nf
        cla(ax_det);
        hold(ax_det, 'on');
        det_r_idx = double(det_result.det_r{frame_idx});
        det_v_idx = double(det_result.det_v{frame_idx});
        clu_ids = det_result.clu_ids{frame_idx};
        n_clu = double(det_result.n_clu(frame_idx));
        n_det = numel(det_r_idx);
        if n_det > 0
            plot(ax_det, v_disp(det_v_idx), r_disp(det_r_idx), ...
                '+', 'Color', [0.45 0.45 0.45], 'MarkerSize', 4, 'LineWidth', 0.8);
        end
        cmap_clu = lines(max(n_clu, 1));
        for clu_idx = 1:n_clu
            idx_ci = find(clu_ids == clu_idx);
            ri = det_r_idx(idx_ci);
            vi = det_v_idx(idx_ci);
            ri_m = min(max(round(mean(ri)), 1), numel(r_disp));
            vi_m = min(max(round(mean(vi)), 1), numel(v_disp));
            plot(ax_det, v_disp(vi), r_disp(ri), '.', 'Color', cmap_clu(clu_idx, :), 'MarkerSize', 10);
            plot(ax_det, v_disp(vi_m), r_disp(ri_m), 'o', 'Color', cmap_clu(clu_idx, :), ...
                'MarkerSize', 14, 'LineWidth', 2);
            text(ax_det, v_disp(vi_m), r_disp(ri_m), sprintf(' #%d', clu_idx), ...
                'Color', cmap_clu(clu_idx, :), 'FontSize', 10, 'FontWeight', 'bold', ...
                'FontName', 'Microsoft YaHei', 'VerticalAlignment', 'bottom');
        end
        hold(ax_det, 'off');
        h_tit_det.String = sprintf('[CFAR + DBSCAN] 第 %d/%d 帧 | 检测点=%d | 聚类数=%d', ...
            frame_ids(frame_idx), n_frames, n_det, n_clu);
        gif_cm_det = write_gif_frame(fig_det, gif_det, frame_idx, delay, gif_cm_det);
        if mod(frame_idx, 200) == 0 || frame_idx == nf
            lg(sprintf('  [绘图] 检测图已完成 %d/%d 帧', frame_idx, nf));
        end
    end
    close(fig_det);
end

%% 3) Angle GIF
if ~isempty(ang_result) && isfield(ang_result, 'has_angle') && ang_result.has_angle
    lg(sprintf('[绘图 3/3] 导出测角动图：%s', gif_ang));
    fig_ang = new_fig(1200, 450);
    ax1 = subplot(1, 2, 1, 'Parent', fig_ang);
    ax2 = subplot(1, 2, 2, 'Parent', fig_ang);
    for axh = [ax1, ax2]
        set(axh, 'FontSize', 11, 'FontName', 'Microsoft YaHei', 'YDir', 'normal');
        grid(axh, 'on');
    end
    xlabel(ax1, sprintf('Az ratio (x%.1f)', k_mono), 'FontSize', 12, 'FontName', 'Microsoft YaHei');
    ylabel(ax1, 'Range (m)', 'FontSize', 12, 'FontName', 'Microsoft YaHei');
    title(ax1, 'Range-Azimuth', 'FontSize', 12, 'FontName', 'Microsoft YaHei');
    xlabel(ax2, sprintf('El ratio (x%.1f)', k_mono), 'FontSize', 12, 'FontName', 'Microsoft YaHei');
    ylabel(ax2, 'Range (m)', 'FontSize', 12, 'FontName', 'Microsoft YaHei');
    title(ax2, 'Range-Elevation', 'FontSize', 12, 'FontName', 'Microsoft YaHei');
    h_sg = sgtitle(fig_ang, '', 'FontSize', 12, 'Interpreter', 'none', 'FontName', 'Microsoft YaHei');

    gif_cm_ang = [];
    for frame_idx = 1:nf
        cla(ax1);
        cla(ax2);
        r_m = double(ang_result.r_m{frame_idx});
        v_m = double(ang_result.v_m{frame_idx});
        az_m = double(ang_result.az_m{frame_idx});
        el_m = double(ang_result.el_m{frame_idx});
        sz_m = double(ang_result.sz_m{frame_idx});
        n_ang = numel(r_m);
        if n_ang > 0
            scatter(ax1, az_m(:), r_m(:), sz_m(:), v_m(:), 'filled');
            clim(ax1, v_range);
            colormap(ax1, hsv(64));
            colorbar(ax1);
            scatter(ax2, el_m(:), r_m(:), sz_m(:), v_m(:), 'filled');
            clim(ax2, v_range);
            colormap(ax2, hsv(64));
            colorbar(ax2);
        else
            text(0.5, 0.5, '无检测目标', 'Parent', ax1, 'Units', 'normalized', ...
                'HorizontalAlignment', 'center', 'FontSize', 14, 'Color', 'k', 'FontName', 'Microsoft YaHei');
            text(0.5, 0.5, '无检测目标', 'Parent', ax2, 'Units', 'normalized', ...
                'HorizontalAlignment', 'center', 'FontSize', 14, 'Color', 'k', 'FontName', 'Microsoft YaHei');
        end
        h_sg.String = sprintf('单脉冲测角 | 第 %d/%d 帧 | 保留目标=%d | 颜色表示速度 (m/s)', ...
            frame_ids(frame_idx), n_frames, n_ang);
        gif_cm_ang = write_gif_frame(fig_ang, gif_ang, frame_idx, delay, gif_cm_ang);
        if mod(frame_idx, 200) == 0 || frame_idx == nf
            lg(sprintf('  [绘图] 测角图已完成 %d/%d 帧', frame_idx, nf));
        end
    end
    close(fig_ang);
end

lg('===== 绘图导出完成 =====');
lg(sprintf('  RD 动图：%s', gif_rd));
if ~isempty(det_result)
    lg(sprintf('  检测动图：%s', gif_det));
end
if ~isempty(ang_result) && isfield(ang_result, 'has_angle') && ang_result.has_angle
    lg(sprintf('  测角动图：%s', gif_ang));
end

if nargout > 0
    out.gif_rd = gif_rd;
    out.gif_det = gif_det;
    out.gif_ang = gif_ang;
end
end

function fig = new_fig(w, h)
%NEW_FIG 创建一个隐藏的绘图窗口。
%
% 输入：
%   w - 窗口宽度
%   h - 窗口高度
% 输出：
%   fig - figure 句柄
% 作用：
%   为 GIF 导出准备固定尺寸的绘图窗口。
%   单独保留这个小函数的原因是：不同图统一窗口尺寸后，导出的动画更稳定，
%   后续如果要统一改图像大小，只需要改这一处。

fig = figure('Visible', 'off', 'Position', [0 0 w h], 'Color', 'w');
end

function rd_dB = amplitude_to_db(rd_full, r_mask, v_mask)
%AMPLITUDE_TO_DB 将 RD 幅度图转换为 dB。
%
% 输入：
%   rd_full - 原始 RD 复数矩阵
%   r_mask  - 距离掩膜
%   v_mask  - 速度掩膜
% 输出：
%   rd_dB   - dB 幅度图
% 作用：
%   为绘图阶段统一转换显示尺度。
%   单独保留这个小函数的原因是：显示尺度转换规则常常需要独立调整，
%   抽出来后更容易统一所有图像的显示口径。

rd_dB = 20 * log10(abs(rd_full(r_mask, v_mask)) + eps);
end

function cm = write_gif_frame(fig, gif_path, frame_idx, delay, cm)
%WRITE_GIF_FRAME 将当前 figure 写入 GIF。
%
% 输入：
%   fig       - figure 句柄
%   gif_path  - GIF 输出路径
%   frame_idx - 当前帧编号
%   delay     - 帧间隔
%   cm        - 已有调色板
% 输出：
%   cm        - 更新后的调色板
% 作用：
%   支持逐帧写出动画文件。
%   单独保留这个小函数的原因是：GIF 写盘属于通用导出动作，
%   和具体画什么内容无关，拆开后主绘图流程更容易阅读。

drawnow;
frm = getframe(fig);
im = frame2im(frm);
if frame_idx == 1
    [imind, cm] = rgb2ind(im, 256);
    imwrite(imind, cm, gif_path, 'gif', 'Loopcount', inf, 'DelayTime', delay);
else
    imind = rgb2ind(im, cm);
    imwrite(imind, cm, gif_path, 'gif', 'WriteMode', 'append', 'DelayTime', delay);
end
end
