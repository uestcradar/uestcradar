%% test_plot_discriminator.m 可视化 X256B 实测方向图得到的单脉冲鉴角曲线
%
% 鉴角原理：比幅单脉冲，**幅值走模值、符号走 1bit 相位**
%   raz = |Δaz / Σ|     方位差/和口（模值，V 形，δ=0 处为谷）
%   rel = |Δel / Σ|     俯仰差/和口（模值）
%   左右/上下由 sign(Re(Δ/Σ·exp(-1iφ))) 单独给出（φ = cfg.angle.phi_az/phi_el），本脚本不画符号。
%
% 为什么不画 real(Δ/Σ)：Σ/Δ 相对相位由硬件决定且随频率变化（实测远场判别器相角
% 约 -105°(方位) / +125°(俯仰)），real() 只保留投影分量，会丢 19%~64% 的幅值。
% 模值对该相位完全免疫，代价是失去符号——这正是 1bit 符号位要补的那一位。
%
% 本脚本直接解析 接收/方位、接收/俯仰 下的 和口/差口 .ccc 文件，
% 沿 el=0（方位）与 az=0（俯仰）切割，按各自波位中心角重定位为偏移角 δ，
% 画出模值曲线及其左右半轴各自的单调范围，并拟合 |δ|≤ROI 内的平均斜率。
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

%% 2. 方位鉴角曲线 raz(δaz)：沿 el=0 切割，每个方位波位一条曲线（模值）
el0_idx = find(abs(sum_az.el_axis) < 1e-9, 1);
if isempty(el0_idx), [~, el0_idx] = min(abs(sum_az.el_axis)); end

az_anchors = sort(unique(sum_az.beams(:, 1)));   % 各方位波位中心角
[az_axis_s, az_ord] = sort(sum_az.az_axis(:));

figure('Name', '方位鉴角曲线 raz = |Δaz/Σ|', 'Color', 'w');
hold on; grid on;
for i = 1:numel(az_anchors)
    b = find(sum_az.beams(:, 1) == az_anchors(i), 1);
    raz_abs = abs(diff_az.data(el0_idx, :, f_idx, b) ./ (sum_az.data(el0_idx, :, f_idx, b) + eps));
    raz_abs = raz_abs(az_ord);                   % 按方位角升序重排
    daz = az_axis_s.' - az_anchors(i);           % 重定位到偏移角 δaz
    plot(daz, raz_abs, '.-', 'LineWidth', 1.4, ...
        'DisplayName', sprintf('波位 az=%+d°', az_anchors(i)));
end
xlabel('方位偏移 δaz (°)');
ylabel('raz = |\Deltaaz / \Sigma|');
title('方位鉴角曲线（el=0 切割，实测模值；V 形，δ=0 为谷）');
legend('Location', 'northwest');
yl = ylim;
plot([0 0], yl, 'k--', 'HandleVisibility', 'off');                  % δ=0 波束中心
plot([-roi_deg -roi_deg], yl, 'r:', 'HandleVisibility', 'off');     % ±ROI 边界
plot([ roi_deg  roi_deg], yl, 'r:', 'HandleVisibility', 'off');

%% 3. 俯仰鉴角曲线 rel(δel)：沿 az=0 切割，每个俯仰波位一条曲线（模值）
az0_idx = find(abs(sum_el.az_axis) < 1e-9, 1);
if isempty(az0_idx), [~, az0_idx] = min(abs(sum_el.az_axis)); end

el_anchors = sort(unique(sum_el.beams(:, 2)));   % 各俯仰波位中心角
[el_axis_s, el_ord] = sort(sum_el.el_axis(:));

figure('Name', '俯仰鉴角曲线 rel = |Δel/Σ|', 'Color', 'w');
hold on; grid on;
for i = 1:numel(el_anchors)
    b = find(sum_el.beams(:, 2) == el_anchors(i), 1);
    rel_abs = abs(diff_el.data(:, az0_idx, f_idx, b) ./ (sum_el.data(:, az0_idx, f_idx, b) + eps));
    rel_abs = rel_abs(el_ord);                   % 按俯仰角升序重排
    del = el_axis_s.' - el_anchors(i);           % 重定位到偏移角 δel
    plot(del, rel_abs, '.-', 'LineWidth', 1.4, ...
        'DisplayName', sprintf('波位 el=%+d°', el_anchors(i)));
end
xlabel('俯仰偏移 δel (°)');
ylabel('rel = |\Deltael / \Sigma|');
title('俯仰鉴角曲线（az=0 切割，实测模值；V 形，δ=0 为谷）');
legend('Location', 'northwest');
yl = ylim;
plot([0 0], yl, 'k--', 'HandleVisibility', 'off');                  % δ=0 波束中心
plot([-roi_deg -roi_deg], yl, 'r:', 'HandleVisibility', 'off');     % ±ROI 边界
plot([ roi_deg  roi_deg], yl, 'r:', 'HandleVisibility', 'off');

%% 4. 半轴单调范围 + 半轴斜率 + 左右不对称度
% 模值曲线以 δ=0 为谷，左右半轴各自单调；只有单调段内模值反查才无歧义，
% 所以这里报的是"半轴可用范围"，而不是有符号曲线的峰值-谷值区间。
fprintf('\n=== 方位鉴角（|δaz| ≤ %.1f° 内拟合半轴斜率；半轴范围 = 无歧义反查区间） ===\n', roi_deg);
for i = 1:numel(az_anchors)
    b = find(sum_az.beams(:, 1) == az_anchors(i), 1);
    raz_abs = abs(diff_az.data(el0_idx, :, f_idx, b) ./ (sum_az.data(el0_idx, :, f_idx, b) + eps));
    raz_abs = raz_abs(az_ord);
    daz = az_axis_s.' - az_anchors(i);
    [dPos, dNeg] = modulus_mono_bounds(daz, raz_abs);
    kp = half_slope(daz, raz_abs, roi_deg, +1);
    kn = half_slope(daz, raz_abs, roi_deg, -1);
    fprintf('  波位 az=%+4d°:  k+ = %+.4f  k- = %+.4f  半轴范围 [+0, %+5.2f°] / [%+5.2f°, -0]  不对称 %+.1f%%\n', ...
        az_anchors(i), kp, kn, dPos, dNeg, 100 * asym(kp, kn));
end

fprintf('=== 俯仰鉴角（|δel| ≤ %.1f° 内拟合半轴斜率；半轴范围 = 无歧义反查区间） ===\n', roi_deg);
for i = 1:numel(el_anchors)
    b = find(sum_el.beams(:, 2) == el_anchors(i), 1);
    rel_abs = abs(diff_el.data(:, az0_idx, f_idx, b) ./ (sum_el.data(:, az0_idx, f_idx, b) + eps));
    rel_abs = rel_abs(el_ord);
    del = el_axis_s.' - el_anchors(i);
    [dPos, dNeg] = modulus_mono_bounds(del, rel_abs);
    kp = half_slope(del, rel_abs, roi_deg, +1);
    kn = half_slope(del, rel_abs, roi_deg, -1);
    fprintf('  波位 el=%+4d°:  k+ = %+.4f  k- = %+.4f  半轴范围 [+0, %+5.2f°] / [%+5.2f°, -0]  不对称 %+.1f%%\n', ...
        el_anchors(i), kp, kn, dPos, dNeg, 100 * asym(kp, kn));
end

fprintf('\n提示：模值曲线不含符号。左右/上下方向由 sign(Re(Δ/Σ·exp(-1iφ))) 提供，映射关系写在\n');
fprintf('      cfg.angle.phi_az / phi_el（判别器相位，逐批次标定）+ sign_az / sign_el（整体正负号）。\n');
fprintf('      若实测结果左右或上下"恒定偏一侧"，改的是 phi_*；只有整体反号才改 sign_*。\n');

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

function [dPos, dNeg] = modulus_mono_bounds(delta, y)
%MODULUS_MONO_BOUNDS 模值曲线 |δ|→|Δ/Σ| 的左右半轴无歧义反查范围。
% 模值曲线以 δ=0 为谷、半轴各自单调，取半轴上首个回落点（滑窗平滑后）为该侧边界。
% 返回：dPos = 正半轴 |δ| 上界；dNeg = 负半轴的 δ 下界（负值）。
    delta = delta(:); y = y(:);
    dPos = half_bound(delta, y, +1);
    dNeg = half_bound(delta, y, -1);
end

function b = half_bound(delta, y, sgn)
%HALF_BOUND 单侧（sgn=+1 取 δ≥0，sgn=-1 取 δ≤0）按 |δ| 升序的首个单调递增段上界。
    if sgn > 0, sel = delta >= 0; else, sel = delta <= 0; end
    d = delta(sel); v = y(sel);
    [~, ord] = sort(abs(d));
    v = v(ord); d = d(ord);
    b = d(end);
    if numel(v) < 3, return; end
    win = max(3, round(numel(v) * 0.02));        % 平滑窗（约 2% 点数）
    if mod(win, 2) == 0, win = win + 1; end
    vs = movmean(v, win);
    k = find(diff(vs) <= 0, 1, 'first');         % 模值回落 = 该半轴单调段结束
    if ~isempty(k), b = d(k); end
    if ~isfinite(b), b = d(end); end
end

function k = half_slope(delta, y, roi, sgn)
%HALF_SLOPE 半轴 |δ| ≤ roi 内拟合模值随 |δ| 的斜率（对左右不对称度作比较用）。
    if sgn > 0, sel = delta >= 0 & delta <= roi; else, sel = delta <= 0 & delta >= -roi; end
    if nnz(sel) < 2, k = NaN; return; end
    p = polyfit(abs(delta(sel)), y(sel), 1);
    k = p(1);
end

function a = asym(kp, kn)
%ASYM 左右/上下半轴斜率的不对称度（正=右侧更陡）。
    if ~isfinite(kp) || ~isfinite(kn) || abs(kp) + abs(kn) < eps, a = NaN; return; end
    a = 2 * (kp - kn) / (abs(kp) + abs(kn));
end
