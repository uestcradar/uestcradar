%% test_plot_discriminator.m 可视化 X256B 实测方向图得到的单脉冲鉴角曲线
%
% 鉴角原理：比幅单脉冲 Δ/Σ 比值
%   raz = real(Δaz / Σ)     方位差/和口
%   rel = real(Δel / Σ)     俯仰差/和口
%
% 本脚本直接解析 接收/方位、接收/俯仰 下的 和口/差口 .ccc 文件，
% 沿 el=0（方位）与 az=0（俯仰）切割，按各自波位中心角重定位为
% 偏移角 δ，只画出每条曲线的单调段（峰值与谷值之间的无歧义区间），
% 并拟合局部斜率 k、打印单调范围到命令行。
%
% 这些原始曲线正是 src/mono_angle.m 中 generate_measured_lut_local
% 提取锚点、再按波位中心角插值成逐波位 LUT 的依据。

clear; close all; clc;

%% 0. 环境与参数
this_dir = fileparts(mfilename('fullpath'));
addpath(this_dir);
addpath(genpath(fullfile(this_dir, 'src')));

pattern_dir = fullfile(this_dir, 'X256B-A24147');
fc_hz   = 9.5e9;   % 载频，自动选最近实测频点
roi_deg = 5.0;     % 鉴角曲线关注范围 ±ROI（用于斜率拟合与边界标记）
lut_sign = 1;      % 仅用于打印提示，本脚本画原始曲线不施加符号

%% 1. 解析 4 个接收方向图（和口/差口 × 方位/俯仰）
sum_az  = load_ccc_pattern(ccc_in(fullfile(pattern_dir, '接收', '方位', '和口')));
diff_az = load_ccc_pattern(ccc_in(fullfile(pattern_dir, '接收', '方位', '差口')));
sum_el  = load_ccc_pattern(ccc_in(fullfile(pattern_dir, '接收', '俯仰', '和口')));
diff_el = load_ccc_pattern(ccc_in(fullfile(pattern_dir, '接收', '俯仰', '差口')));

[~, f_idx] = min(abs(sum_az.freqs * 1e6 - fc_hz));
fc_used = sum_az.freqs(f_idx) * 1e6;
fprintf('[方向图] 使用频点 %.0f MHz（目标 %.2f GHz）\n', fc_used / 1e6, fc_hz / 1e9);
fprintf('[方向图] 方位文件 波位=%s，el轴=%d点，az轴=%d点\n', ...
    mat2str(sum_az.beams), numel(sum_az.el_axis), numel(sum_az.az_axis));
fprintf('[方向图] 俯仰文件 波位=%s，el轴=%d点，az轴=%d点\n', ...
    mat2str(sum_el.beams), numel(sum_el.el_axis), numel(sum_el.az_axis));

%% 2. 方位鉴角曲线 raz(δaz)：沿 el=0 切割，每个方位波位一条曲线（仅单调段）
el0_idx = find(abs(sum_az.el_axis) < 1e-9, 1);
if isempty(el0_idx), [~, el0_idx] = min(abs(sum_az.el_axis)); end

az_anchors = sort(unique(sum_az.beams(:, 1)));   % 各方位波位中心角
[az_axis_s, az_ord] = sort(sum_az.az_axis(:));

figure('Name', '方位鉴角曲线 raz = real(Δaz/Σ)', 'Color', 'w');
hold on; grid on;
for i = 1:numel(az_anchors)
    b = find(sum_az.beams(:, 1) == az_anchors(i), 1);
    raz_abs = real(diff_az.data(el0_idx, :, f_idx, b) ./ (sum_az.data(el0_idx, :, f_idx, b) + eps));
    raz_abs = raz_abs(az_ord);                   % 按方位角升序重排
    daz = az_axis_s.' - az_anchors(i);           % 重定位到偏移角 δaz
    [dL, dR] = monotonic_bounds(daz, raz_abs);   % 单调区间边界
    sel = daz >= dL & daz <= dR;
    plot(daz(sel), raz_abs(sel), '.-', 'LineWidth', 1.4, ...
        'DisplayName', sprintf('波位 az=%+d°', az_anchors(i)));
end
xlabel('方位偏移 δaz (°)');
ylabel('raz = real(Δaz/Σ)');
title('方位鉴角曲线（el=0 切割，实测，仅单调段）');
legend('Location', 'northwest');
yl = ylim;
plot([0 0], yl, 'k--', 'HandleVisibility', 'off');                  % δ=0 波束中心
plot([-roi_deg -roi_deg], yl, 'r:', 'HandleVisibility', 'off');     % ±ROI 边界
plot([ roi_deg  roi_deg], yl, 'r:', 'HandleVisibility', 'off');

%% 3. 俯仰鉴角曲线 rel(δel)：沿 az=0 切割，每个俯仰波位一条曲线（仅单调段）
az0_idx = find(abs(sum_el.az_axis) < 1e-9, 1);
if isempty(az0_idx), [~, az0_idx] = min(abs(sum_el.az_axis)); end

el_anchors = sort(unique(sum_el.beams(:, 2)));   % 各俯仰波位中心角
[el_axis_s, el_ord] = sort(sum_el.el_axis(:));

figure('Name', '俯仰鉴角曲线 rel = real(Δel/Σ)', 'Color', 'w');
hold on; grid on;
for i = 1:numel(el_anchors)
    b = find(sum_el.beams(:, 2) == el_anchors(i), 1);
    rel_abs = real(diff_el.data(:, az0_idx, f_idx, b) ./ (sum_el.data(:, az0_idx, f_idx, b) + eps));
    rel_abs = rel_abs(el_ord);                   % 按俯仰角升序重排
    del = el_axis_s.' - el_anchors(i);           % 重定位到偏移角 δel
    [dL, dR] = monotonic_bounds(del, rel_abs);   % 单调区间边界
    sel = del >= dL & del <= dR;
    plot(del(sel), rel_abs(sel), '.-', 'LineWidth', 1.4, ...
        'DisplayName', sprintf('波位 el=%+d°', el_anchors(i)));
end
xlabel('俯仰偏移 δel (°)');
ylabel('rel = real(Δel/Σ)');
title('俯仰鉴角曲线（az=0 切割，实测，仅单调段）');
legend('Location', 'northwest');
yl = ylim;
plot([0 0], yl, 'k--', 'HandleVisibility', 'off');                  % δ=0 波束中心
plot([-roi_deg -roi_deg], yl, 'r:', 'HandleVisibility', 'off');     % ±ROI 边界
plot([ roi_deg  roi_deg], yl, 'r:', 'HandleVisibility', 'off');

%% 4. 局部线性斜率 + 单调范围汇总（用于核对线性度、符号与无歧义覆盖）
fprintf('\n=== 方位鉴角（|δaz| ≤ %.1f° 内拟合斜率；单调范围 = 无歧义区间） ===\n', roi_deg);
for i = 1:numel(az_anchors)
    b = find(sum_az.beams(:, 1) == az_anchors(i), 1);
    raz_abs = real(diff_az.data(el0_idx, :, f_idx, b) ./ (sum_az.data(el0_idx, :, f_idx, b) + eps));
    raz_abs = raz_abs(az_ord);
    daz = az_axis_s.' - az_anchors(i);
    [dL, dR] = monotonic_bounds(daz, raz_abs);
    sel = abs(daz) <= roi_deg;
    p = polyfit(daz(sel), raz_abs(sel), 1);
    fprintf('  波位 az=%+4d°:  k_az = %+.4f  单调范围 [%+6.2f°, %+6.2f°]\n', ...
        az_anchors(i), p(1), dL, dR);
end

fprintf('=== 俯仰鉴角（|δel| ≤ %.1f° 内拟合斜率；单调范围 = 无歧义区间） ===\n', roi_deg);
for i = 1:numel(el_anchors)
    b = find(sum_el.beams(:, 2) == el_anchors(i), 1);
    rel_abs = real(diff_el.data(:, az0_idx, f_idx, b) ./ (sum_el.data(:, az0_idx, f_idx, b) + eps));
    rel_abs = rel_abs(el_ord);
    del = el_axis_s.' - el_anchors(i);
    [dL, dR] = monotonic_bounds(del, rel_abs);
    sel = abs(del) <= roi_deg;
    p = polyfit(del(sel), rel_abs(sel), 1);
    fprintf('  波位 el=%+4d°:  k_el = %+.4f  单调范围 [%+6.2f°, %+6.2f°]\n', ...
        el_anchors(i), p(1), dL, dR);
end

fprintf('\n提示：若曲线斜率为负（随 δ 增大而比值下降），且代码中实测结果出现\n');
fprintf('镜像（左右/上下反了），应把 cfg.angle.measured_lut_sign 置为 -1。当前配置 lut_sign=%d。\n', lut_sign);

%% =====================================================================
%  局部子函数
% =====================================================================
function f = ccc_in(d)
%CCC_IN 返回目录 d 下第一个 .ccc 文件完整路径；无则返回 ''。
d2 = dir(fullfile(d, '*.ccc'));
if isempty(d2)
    f = '';
    return;
end
f = fullfile(d2(1).folder, d2(1).name);
end

function [dL, dR] = monotonic_bounds(delta, y)
%MONOTONIC_BOUNDS 返回鉴角曲线在 0 附近单调区间的左右边界（与 delta 同单位）。
% delta 需升序。做法：先滑动平均抑制噪声，再找斜率变号点（局部极值），
% 取 0 左侧最接近的极值为左界、0 右侧最接近的极值为右界；某侧无极值则退回该侧端点。
    delta = delta(:); y = y(:);
    if numel(delta) < 3
        dL = delta(1); dR = delta(end); return;
    end
    win = max(3, round(numel(y) * 0.02));        % 平滑窗（约 2% 点数）
    if mod(win, 2) == 0, win = win + 1; end
    ys = movmean(y, win);
    d = diff(ys);
    ext_idx = find(d(1:end-1) .* d(2:end) < 0) + 1;   % 斜率变号 = 局部极值
    if isempty(ext_idx)
        dL = delta(1); dR = delta(end); return;
    end
    left  = ext_idx(delta(ext_idx) < 0);
    right = ext_idx(delta(ext_idx) > 0);
    if isempty(left)
        dL = delta(1);
    else
        [~, k] = max(delta(left));               % 最接近 0 的左侧极值
        dL = delta(left(k));
    end
    if isempty(right)
        dR = delta(end);
    else
        [~, k] = min(delta(right));              % 最接近 0 的右侧极值
        dR = delta(right(k));
    end
end
