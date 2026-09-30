function varargout = mono_angle(varargin)
%MONO_ANGLE 单脉冲测角统一入口（幅值走模值、符号走 1bit 相位）。
%
% 鉴角原理（与硬件约定一致）：
%   幅值  |Δ/Σ|                     → 精确测角。对 Σ/Δ 相对相位完全免疫（含"差值随频率变化"那一项）
%   符号  sign(Re(Δ/Σ · exp(-1i·φ))) → 只需 1bit，区分左右 / 上下
% 偏移角 = 符号 × 模值反查结果。
% 因此 **LUT 模式下必须传入复数比值 Δ/Σ，切勿在调用前取 real()**。
%
% 关于 φ（判别器相位，monopulse_lut.phi_az / phi_el）：
%   Σ/Δ 相对相位由硬件决定（厂商：同批次恒定，但随频率变化），所以"哪根轴承载左右"
%   不能靠猜。实测远场 φ_az ≈ -105°、φ_el ≈ +120°~+125°（后者未精确标定）；
%   真值反查的工作数据 φ_az ≈ -93.5°（0916）与 ≈ -104.6°（0928，相隔 12 天）。
%   **直接取 sign(real()) 等于假设 φ=0**；当 φ 离 0° 超过 90° 时实部只剩加权后的残差、
%   在 δ 两侧同号，表现为"左右判别恒定偏向一侧"（本仓库踩过：beam26 整段航迹 114 帧中
%   Re(Δaz/Σ) 有 99 帧为负，与真值侧别无关，前一半航迹被整体镜像）。
%   故 φ 由 LUT 字段给出，默认 0 = 保持旧的 sign(real) 行为；整体正负号仍由 sign_az/sign_el 承担。
%
%   φ 的取值口径（2026-09-30 定）：**按批次硬编码 + 每批跑 sign_selfcheck.m 自检**，
%   不要改成用直达波在线测。依据：判符号正确的窗口 |Δφ| < 90°，而观测到的 φ 漂移只有
%   11°/12 天 —— 容差是漂移的 8 倍，可固定；同期直达波 arg(R_dw)_az 却变了 96°，
%   用它反而把稳定量换成不稳定量。详见 docs/monopulse_sign_failure.md §7.4b/§7.5。
%
% 硬件约定：阵面差通道为"左减右、上减下"，即目标偏左 / 偏上时 Σ/Δ 落在 φ 方向上。
%
% === 模式 1：LUT 生成（理论模型）===
%   monopulse_lut = mono_angle('generate_lut', k_az, k_el, roi_deg, step_deg, beam_schedule)
%
% === 模式 1b：LUT 生成（实测方向图）===
%   monopulse_lut = mono_angle('generate_measured_lut', pattern_dir, fc_hz, roi_deg, step_deg, beam_schedule)
%   生成的 raz_map/rel_map 为 |Δ/Σ|（无符号、V 形）；符号由调用方写入 .sign_az/.sign_el。
%
% === 模式 2：LUT 查表测角（2D 解耦）===
%   [r_m, v_m, az_m, el_m, sz_m, angle_stats] = mono_angle( ...
%       r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
%       pwr, az_cpx, el_cpx, ~, angle_r_range, angle_v_range, min_display_power_dB, ...
%       monopulse_lut, beam_id)
%   az_cpx/el_cpx 为**复数**比值 Δaz/Σ 与 Δel/Σ（未取 real）。k_mono 位置（第10参）被忽略，占位即可。
%   输出 az_m/el_m 为相对波位中心的角度偏移量（度）。
%   angle_stats(可选第6出参)：超 ±ROI / 反查失败 / 功率剔除的点数统计，字段见 angle_by_lut。
%
% === 模式 3：线性 k_mono 测角（回退兼容，GUI 诊断用，不参与管线）===
%   [r_m, v_m, az_m, el_m, sz_m] = mono_angle( ...
%       r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
%       pwr, az_ratio, el_ratio, k_mono, angle_r_range, angle_v_range, min_display_power_dB)
%   注意：此模式沿用实值比值，az_m/el_m 输出的是鉴角比值（ratio/k_mono），非物理角度。

% ---- 模式分发 ----
if ischar(varargin{1}) && strcmp(varargin{1}, 'generate_lut')
    % === 理论模型 LUT 生成模式 ===
    varargout{1} = generate_lut_local(varargin{2:end});
    return;
end
if ischar(varargin{1}) && strcmp(varargin{1}, 'generate_measured_lut')
    % === 实测方向图 LUT 生成模式 ===
    varargout{1} = generate_measured_lut_local(varargin{2:end});
    return;
end

% === 测角模式 ===
[r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
    pwr, az_ratio, el_ratio, k_mono, angle_r_range, angle_v_range, min_display_power_dB] = ...
    deal(varargin{1:13});

% 检测是否有 LUT 参数
use_lut = (nargin >= 14) && isstruct(varargin{14}) && ~isempty(varargin{14});
if use_lut
    % 模值测角需要复数比值：|Δ/Σ| 取幅值、sign(Re(Δ/Σ·exp(-1iφ))) 取符号。实数输入会被
    % 静默地当成 |Re| 使用，与模值 LUT 相差约 3.5×，必须显式拦截。
    if isreal(az_ratio) || isreal(el_ratio)
        error('mono_angle:LutNeedsComplexRatio', ...
            ['LUT 模值测角需要复数比值 Δ/Σ，检测到实数输入。' ...
             '请传入 rd1_sub./(rd_sub+eps)，不要在调用前取 real()。']);
    end
    monopulse_lut = varargin{14};
    beam_id = 1;
    if nargin >= 15
        beam_id = varargin{15};
    end
    [r_m, v_m, az_m, el_m, sz_m, angle_stats] = angle_by_lut( ...
        r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
        pwr, az_ratio, el_ratio, monopulse_lut, beam_id, ...
        angle_r_range, angle_v_range, min_display_power_dB);
else
    [r_m, v_m, az_m, el_m, sz_m] = angle_by_kmono( ...
        r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
        pwr, az_ratio, el_ratio, k_mono, ...
        angle_r_range, angle_v_range, min_display_power_dB);
    angle_stats = struct('n_clu', 0, 'n_el_oob', 0, 'n_az_oob', 0, ...
        'n_el_nan', 0, 'n_az_nan', 0, 'n_pwr_reject', 0, 'n_kept', 0, ...
        'max_abs_el_oob', 0, 'max_abs_az_oob', 0);
end

varargout = {r_m, v_m, az_m, el_m, sz_m, angle_stats};
end

% =====================================================================
% 线性 k_mono 测角（原有逻辑）
% =====================================================================
function [r_m, v_m, az_m, el_m, sz_m] = angle_by_kmono( ...
    r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
    pwr, az_ratio, el_ratio, k_mono, ...
    angle_r_range, angle_v_range, min_display_power_dB)

r_m = [];
v_m = [];
az_m = [];
el_m = [];
sz_m = [];

for ci = 1:n_clu
    idx_ci = find(clu_ids == ci);
    if isempty(idx_ci), continue; end

    ri = det_r_idx(idx_ci);
    vi = det_v_idx(idx_ci);
    pwr_ci = pwr(sub2ind(size(pwr), ri, vi));
    [~, best] = max(pwr_ci);
    rb = ri(best);
    vb = vi(best);

    r_val = r_disp(rb);
    v_val = v_disp(vb);
    if r_val < angle_r_range(1) || r_val > angle_r_range(2), continue; end
    if v_val < angle_v_range(1) || v_val > angle_v_range(2), continue; end

    peak_pwr_dB = 10 * log10(double(pwr_ci(best)) + eps);
    if peak_pwr_dB < min_display_power_dB, continue; end

    r_m(end + 1) = r_val; %#ok<AGROW>
    v_m(end + 1) = v_val; %#ok<AGROW>
    az_m(end + 1) = az_ratio(rb, vb) / k_mono; %#ok<AGROW>
    el_m(end + 1) = el_ratio(rb, vb) / k_mono; %#ok<AGROW>
    sz_m(end + 1) = max(30, peak_pwr_dB - min_display_power_dB + 10); %#ok<AGROW>
end
end

% =====================================================================
% LUT 查表测角（2D 解耦）
% =====================================================================
function [r_m, v_m, az_m, el_m, sz_m, angle_stats] = angle_by_lut( ...
    r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
    pwr, az_cpx, el_cpx, monopulse_lut, beam_id, ...
    angle_r_range, angle_v_range, min_display_power_dB)

r_m = [];
v_m = [];
az_m = [];
el_m = [];
sz_m = [];

% 单脉冲测角统计：本帧各聚类中偏移超出 ±ROI 或反查失败的点数
angle_stats = struct( ...
    'n_clu',          n_clu, ...  % 输入聚类数
    'n_el_oob',       0,     ...  % 俯仰偏移数值超 ±ROI 被剔除
    'n_az_oob',       0,     ...  % 方位偏移数值超 ±ROI 被剔除
    'n_el_nan',       0,     ...  % 俯仰反查失败(NaN)被剔除
    'n_az_nan',       0,     ...  % 方位反查失败(NaN)被剔除
    'n_pwr_reject',   0,     ...  % 功率门限被剔除
    'n_kept',         0,     ...  % 成功输出
    'max_abs_el_oob', 0,     ...  % 超限点中最大 |俯仰偏移| (°)
    'max_abs_az_oob', 0,     ...  % 超限点中最大 |方位偏移| (°)
    'proj_az_sum',    0,     ...  % 符号自检：Σ|Re(c_az·exp(-1iφ_az))|
    'abs_az_sum',     0,     ...  % 符号自检：Σ|c_az|
    'proj_el_sum',    0,     ...  % 符号自检：Σ|Re(c_el·exp(-1iφ_el))|
    'abs_el_sum',     0,     ...  % 符号自检：Σ|c_el|
    'n_sc_pairs',     0);    ...  % 符号自检：参与累加的目标格数

if isempty(det_r_idx) || n_clu <= 0, return; end

% 选取当前波位的 LUT
if isfield(monopulse_lut, 'data') && iscell(monopulse_lut.data)
    if beam_id <= numel(monopulse_lut.data)
        lut = monopulse_lut.data{beam_id};
    else
        lut = monopulse_lut.data{1};
    end
else
    lut = monopulse_lut;
end

vec_az = lut.az_grid;
vec_el = lut.el_grid;
roi_deg = monopulse_lut.roi_deg;

% 批次级符号位：把硬件符号位（左减右 / 上减下）映射到偏移角的正方向。
sign_az = 1;
sign_el = 1;
if isfield(monopulse_lut, 'sign_az') && ~isempty(monopulse_lut.sign_az)
    sign_az = double(monopulse_lut.sign_az);
end
if isfield(monopulse_lut, 'sign_el') && ~isempty(monopulse_lut.sign_el)
    sign_el = double(monopulse_lut.sign_el);
end

% 判别器相位 φ(°)：δ>0 时 arg(Δ/Σ) 的实测值。默认 0 = 旧的 sign(real()) 行为。
% 侧别信息落在复平面 φ 方向上；φ 偏离 0° 过多时实部不再随侧别换号（见文件头）。
phi_az = 0;
phi_el = 0;
if isfield(monopulse_lut, 'phi_az') && ~isempty(monopulse_lut.phi_az)
    phi_az = double(monopulse_lut.phi_az);
end
if isfield(monopulse_lut, 'phi_el') && ~isempty(monopulse_lut.phi_el)
    phi_el = double(monopulse_lut.phi_el);
end
proj_az = exp(-1i * deg2rad(phi_az));
proj_el = exp(-1i * deg2rad(phi_el));

% 预提取 δaz=0 剖面（Step 1 用），存的是 |Δel/Σ|
[~, idx_az0] = min(abs(vec_az - 0.0));
vec_rel_az0 = lut.rel_map(:, idx_az0);

for ci = 1:n_clu
    idx_ci = find(clu_ids == ci);
    if isempty(idx_ci), continue; end

    ri = det_r_idx(idx_ci);
    vi = det_v_idx(idx_ci);
    pwr_ci = pwr(sub2ind(size(pwr), ri, vi));
    [~, best] = max(pwr_ci);
    rb = ri(best);
    vb = vi(best);

    r_val = r_disp(rb);
    v_val = v_disp(vb);
    if r_val < angle_r_range(1) || r_val > angle_r_range(2), continue; end
    if v_val < angle_v_range(1) || v_val > angle_v_range(2), continue; end

    % 复数比值 Δ/Σ：幅值定角度、符号定方向（先投影到判别器相位 φ 再取实部）
    c_az = double(az_cpx(rb, vb));
    c_el = double(el_cpx(rb, vb));
    p_az = real(c_az * proj_az);
    p_el = real(c_el * proj_el);
    s_az = sign(p_az);
    s_el = sign(p_el);

    % 符号自检累加（见 sign_selfcheck.m）：投影效率 |Re(c·e^{-iφ})|/|c| 与侧别无关，
    % φ 落在判别器轴上时趋近 1、偏 90° 时趋近 0。对每个有复比值的目标格累加，
    % 不加门限、不提前返回 —— 该统计量只用于事后报警，不参与测角本身。
    angle_stats.proj_az_sum = angle_stats.proj_az_sum + abs(p_az);
    angle_stats.abs_az_sum  = angle_stats.abs_az_sum  + abs(c_az);
    angle_stats.proj_el_sum = angle_stats.proj_el_sum + abs(p_el);
    angle_stats.abs_el_sum  = angle_stats.abs_el_sum  + abs(c_el);
    angle_stats.n_sc_pairs  = angle_stats.n_sc_pairs  + 1;

    % Step 1: 用 |Δel/Σ| 在符号位指定的半轴上反查俯仰偏移
    el_off = invert_half_local(vec_el, vec_rel_az0, abs(c_el), s_el, sign_el);
    if isnan(el_off)
        angle_stats.n_el_nan = angle_stats.n_el_nan + 1;
        continue;
    end
    if abs(el_off) > roi_deg
        angle_stats.n_el_oob = angle_stats.n_el_oob + 1;
        angle_stats.max_abs_el_oob = max(angle_stats.max_abs_el_oob, abs(el_off));
        continue;
    end
    el_off = double(el_off);

    % Step 2: 在 el=el_off 切片上，用 |Δaz/Σ| 在符号位指定的半轴上反查方位偏移
    vec_raz_slice = interp2(lut.az_grid, lut.el_grid, lut.raz_map, ...
                            vec_az, repmat(el_off, size(vec_az)), 'linear');
    az_off = invert_half_local(vec_az, vec_raz_slice(:), abs(c_az), s_az, sign_az);
    if isnan(az_off)
        angle_stats.n_az_nan = angle_stats.n_az_nan + 1;
        continue;
    end
    if abs(az_off) > roi_deg
        angle_stats.n_az_oob = angle_stats.n_az_oob + 1;
        angle_stats.max_abs_az_oob = max(angle_stats.max_abs_az_oob, abs(az_off));
        continue;
    end

    peak_pwr_dB = 10 * log10(double(pwr_ci(best)) + eps);
    if peak_pwr_dB < min_display_power_dB
        angle_stats.n_pwr_reject = angle_stats.n_pwr_reject + 1;
        continue;
    end

    r_m(end + 1) = r_val; %#ok<AGROW>
    v_m(end + 1) = v_val; %#ok<AGROW>
    az_m(end + 1) = az_off; %#ok<AGROW>
    el_m(end + 1) = el_off; %#ok<AGROW>
    sz_m(end + 1) = max(30, peak_pwr_dB - min_display_power_dB + 10); %#ok<AGROW>
    angle_stats.n_kept = angle_stats.n_kept + 1;
end
end

% =====================================================================
% 模值半轴反查：|Δ/Σ| → |δ|，符号由硬件符号位 + 批次级 sign 决定
% =====================================================================
function d = invert_half_local(grid, map, m, s_hw, s_axis)
%INVERT_HALF_LOCAL 用模值 m 在指定半轴上反查偏移角 δ。
%   grid   - 有符号角度栅格（-ROI:step:ROI）
%   map    - 与 grid 同长度的 |Δ/Σ| 曲线（V 形，在 grid=0 处为谷）
%   m      - 实测模值 |Δ/Σ|（非负）
%   s_hw   - 硬件符号位 sign(Re(Δ/Σ·exp(-1iφ)))，+1=偏左/偏上，-1=偏右/偏下
%   s_axis - 批次级符号位（把硬件方向映射到 δ 正方向）
% 返回带符号的偏移角；反查失败返回 NaN。
% 说明：模值曲线在 δ=0 有折点，故必须知道落在哪一侧才能反查；只取首个单调递增
%       前缀（实测少数边缘波位的模值在 ±ROI 外沿会回落，回落段不可反查）。
% 注意：前缀必须从**曲线自身的谷底**起，不能从 δ=0 起。实测方向图的零深不一定落在
%       栅格点上（实测 az>0 的 15 个波位谷底在 −0.1° 那一格），此时 δ=0→谷底那一步是
%       下降，从 δ=0 起找递增前缀会立刻命中 diff<=0，前缀塌成 1 点而返回 NaN，
%       使整个半轴（实测是负半轴，即目标偏右/偏下时）的点被静默丢光。

d = NaN;
grid = grid(:);
map  = map(:);

% δ 的正负：硬件符号位 × 批次级符号位；符号位为 0（模值≈0）时按下侧处理，影响可忽略
dir = sign(s_hw) * sign(s_axis);
if dir == 0, dir = 1; end

if dir > 0
    sel = grid > 0;
else
    sel = grid < 0;
end
sel = sel | abs(grid) < eps;          % 保证 δ=0 参与，曲线从谷底起
g = grid(sel);
v = map(sel);
if numel(g) < 2, return; end

% 按 |δ| 升序排列 → 模值应随之递增
[ad, ord] = sort(abs(g));
v = v(ord);
[adu, ia] = unique(ad);
vu = v(ia);
if numel(adu) < 2, return; end

% 从前缀起点：改为曲线自身的谷底（实测零深可能落在 δ=0 的邻格，见头注释）
[~, imin] = min(vu);
adu = adu(imin:end);
vu  = vu(imin:end);
if numel(adu) < 2, return; end

% 只保留首个单调递增前缀，避免非单调段的歧义反查
kmax = find(diff(vu) <= 0, 1, 'first');
if ~isempty(kmax)
    adu = adu(1:kmax);
    vu  = vu(1:kmax);
end
if numel(adu) < 2, return; end

ad_off = interp1(vu, adu, m, 'linear', 'extrap');
if isnan(ad_off), return; end
% 模值低于曲线谷底只可能是噪声（谷底已是该半轴最小值），夹到 0，避免外插出负号把点翻到对面
ad_off = max(ad_off, 0);
d = dir * ad_off;
end

% =====================================================================
% LUT 生成（本地子函数）
% =====================================================================
function monopulse_lut = generate_lut_local(k_az, k_el, roi_deg, step_deg, beam_schedule)
%GENERATE_LUT_LOCAL 理论模型 LUT 生成（模值曲线，无符号）。
% 模型：|Δaz/Σ| = |k_az| * |sin(δaz)| * cos(δel)，|Δel/Σ| = |k_el| * |sin(δel)|
% 方位耦合项 cos(δel) 反映俯仰偏离时有效孔径的缩小；偶函数，不会破坏左右对称性。
% 表里只存模值：曲线是 V 形（|δ| 增则模值增），左右半轴由 1bit 符号位选定后再反查，
% 因此 k_az/k_el 的符号在模值表中不可见，方向由 sign_az/sign_el 承担。

if nargin < 4 || isempty(step_deg), step_deg = 0.1; end
if nargin < 5, beam_schedule = []; end

k_az = double(k_az);
k_el = double(k_el);
roi_deg = double(roi_deg);

angle_offsets = -roi_deg : step_deg : roi_deg;
[AZ_OFF, EL_OFF] = meshgrid(angle_offsets, angle_offsets);

az_rad = deg2rad(AZ_OFF(:));
el_rad = deg2rad(EL_OFF(:));

raz_vals = abs(k_az * sin(az_rad) .* cos(el_rad));
rel_vals = abs(k_el * sin(el_rad));

monopulse_lut = struct();
monopulse_lut.az_grid = angle_offsets;
monopulse_lut.el_grid = angle_offsets;
monopulse_lut.raz_map = reshape(raz_vals, size(AZ_OFF));
monopulse_lut.rel_map = reshape(rel_vals, size(EL_OFF));
monopulse_lut.k_az = k_az;
monopulse_lut.k_el = k_el;
monopulse_lut.roi_deg = roi_deg;
% 符号位（把硬件符号映射到 δ 正方向），调用方可用 cfg.angle.sign_az/sign_el 覆盖
monopulse_lut.sign_az = 1;
monopulse_lut.sign_el = 1;
% 判别器相位 φ(°)，默认 0 = 旧的 sign(real()) 行为；调用方可用 cfg.angle.phi_az/phi_el 覆盖
monopulse_lut.phi_az = 0;
monopulse_lut.phi_el = 0;

% 为每个波位生成独立 LUT（考虑波束中心俯仰对耦合项的影响）
if ~isempty(beam_schedule) && isfield(beam_schedule, 'num_beams') && beam_schedule.num_beams > 1
    num_beams = beam_schedule.num_beams;
    lut_set = cell(1, num_beams);
    for b = 1:num_beams
        beam_el_c = deg2rad(beam_schedule.beam_positions(b, 2));
        el_r_b = el_rad + beam_el_c;
        raz_b = abs(k_az * sin(az_rad) .* cos(el_r_b));
        rel_b = abs(k_el * (sin(el_r_b) - sin(beam_el_c)));

        beam_lut = struct();
        beam_lut.beam_az = beam_schedule.beam_positions(b, 1);
        beam_lut.beam_el = beam_schedule.beam_positions(b, 2);
        beam_lut.az_grid = angle_offsets;
        beam_lut.el_grid = angle_offsets;
        beam_lut.raz_map = reshape(raz_b, size(AZ_OFF));
        beam_lut.rel_map = reshape(rel_b, size(EL_OFF));
        lut_set{b} = beam_lut;
    end
    monopulse_lut.data = lut_set;
    fprintf('[LUT] 已为 %d 个波位生成独立鉴角表（模值 |Δ/Σ|, |k_az|=%.1f, |k_el|=%.1f, ROI=±%.1f°）\n', ...
        num_beams, abs(k_az), abs(k_el), roi_deg);
else
    monopulse_lut.data = {monopulse_lut};
    fprintf('[LUT] 已生成单波位鉴角表（模值 |Δ/Σ|, |k_az|=%.1f, |k_el|=%.1f, ROI=±%.1f°, 步长 %.1f°）\n', ...
        abs(k_az), abs(k_el), roi_deg, step_deg);
end
end

% =====================================================================
% 实测方向图 LUT 生成（本地子函数）
% =====================================================================
function monopulse_lut = generate_measured_lut_local(pattern_dir, fc_hz, roi_deg, step_deg, beam_schedule)
%GENERATE_MEASURED_LUT_LOCAL 用实测远场方向图生成比幅单脉冲鉴角 LUT（支持逐波位）。
% 鉴角原理：raz = |Δaz/Σ|，rel = |Δel/Σ|（**模值**，无符号、V 形）。
% 单基地雷达下发射方向图在 Δ/Σ 比值中约去，故只需接收方向图。
%
% 为什么用模值而不是 real()：
%   (1) Σ/Δ 相对相位由硬件决定且随频率变化，real() 只保留投影分量。实测远场判别器
%       相角约 -105°（方位）/ +120°~+125°（俯仰，未精确标定），real() 仅取到 19%~64% 的幅值——这正是
%       "曲线满量程被压缩数倍、ROI 拒收率偏高"的直接原因。模值对该相位完全免疫。
%   (2) `.ccc` 的和口/差口是分不同会话测的，Σ/Δ 相对相位本身不可信；幅度则可信
%       （外围区 Δ−Σ 电平差在 7 个频点上稳定 = 测量增益差）。模值只用幅度。
%   符号不在这里处理：由查表时"硬件符号位（左减右/上减下）+ lut.sign_az/.sign_el"给出。
%
% 关键点：鉴角曲线斜率随波位扫描角增大而变小（波束展宽），故按波位中心角对
% 实测锚点曲线（方位 0/±15/±30/±45°，俯仰 0/±15/±30/±40°）做插值，为每个
% 波位生成各自曲线。波位数与排布由 beam_schedule 运行时决定，无需预先固定，
% 任意波位间隔（如 5°）都能自动适配。
%
% 输入：
%   pattern_dir    - 实测方向图根目录（含 接收/方位、接收/俯仰 下 和口/差口 的 .ccc）
%   fc_hz          - 载频 (Hz)，自动选最近实测频点
%   roi_deg        - 鉴角角度覆盖 ±ROI (deg)
%   step_deg       - LUT 栅格步长 (deg)
%   beam_schedule  - 波位排布（可选），含 beam_positions [nBeam×2]；为空则只生成法向单表
% 输出：
%   monopulse_lut  - roi_deg + data{nBeam}（每波位 az_grid/el_grid/raz_map/rel_map，均为模值）

% 曲线构建逻辑变更时必须 +1，否则旧缓存会掩盖新代码（历史版本用 cache_lut_sign 区分，
% 已随模值改造作废）。
LUT_CACHE_VERSION = 1;

if nargin < 5, beam_schedule = []; end

% ---- 缓存：参数不变时复用（含波位排布，排布变了自动失效）----
cache_file = fullfile(pattern_dir, 'measured_lut_cache.mat');
if exist(cache_file, 'file') == 2
    S = load(cache_file);  % 整读缓存（旧版缺字段时靠 isfield 判空，避免告警）
    bp_now = [];
    if isstruct(beam_schedule) && isfield(beam_schedule, 'beam_positions')
        bp_now = beam_schedule.beam_positions;
    end
    bp_cmp = isfield(S, 'cache_beam_positions') && isequal(S.cache_beam_positions, bp_now);
    if isfield(S, 'cached_lut') ...
            && isfield(S, 'cache_version') && S.cache_version == LUT_CACHE_VERSION ...
            && isfield(S, 'cache_fc_hz') && abs(S.cache_fc_hz - fc_hz) < 1e-6 ...
            && isfield(S, 'cache_roi_deg') && S.cache_roi_deg == roi_deg ...
            && isfield(S, 'cache_step_deg') && S.cache_step_deg == step_deg ...
            && bp_cmp
        monopulse_lut = S.cached_lut;
        fprintf('[实测LUT] 命中缓存，跳过方向图解析: %s\n', cache_file);
        return;
    end
end

% ---- 定位并解析 4 个接收方向图文件 ----
sum_az_file  = find_ccc_local(fullfile(pattern_dir, '接收', '方位', '和口'));
diff_az_file = find_ccc_local(fullfile(pattern_dir, '接收', '方位', '差口'));
sum_el_file  = find_ccc_local(fullfile(pattern_dir, '接收', '俯仰', '和口'));
diff_el_file = find_ccc_local(fullfile(pattern_dir, '接收', '俯仰', '差口'));
if isempty(sum_az_file) || isempty(diff_az_file) || isempty(sum_el_file) || isempty(diff_el_file)
    error('generate_measured_lut:MissingFile', ...
        '在 %s 下未找到完整的接收和/差方向图(.ccc)，请检查 接收/方位、接收/俯仰 下的 和口/差口 目录', pattern_dir);
end
sum_az  = load_ccc_pattern(sum_az_file);
diff_az = load_ccc_pattern(diff_az_file);
sum_el  = load_ccc_pattern(sum_el_file);
diff_el = load_ccc_pattern(diff_el_file);

% ---- 选最近频点 ----
[~, f_idx] = min(abs(sum_az.freqs * 1e6 - fc_hz));
fc_used = sum_az.freqs(f_idx) * 1e6;

% ---- 栅格 ----
az_grid = -roi_deg : step_deg : roi_deg;
el_grid = -roi_deg : step_deg : roi_deg;

% ---- 提取方位锚点曲线（各波位 el=0 切割，按名义方位角重定位为 δaz）----
el0_idx = find(abs(sum_az.el_axis) < 1e-9, 1);
if isempty(el0_idx), [~, el0_idx] = min(abs(sum_az.el_axis)); end
az_anchors = sort(unique(sum_az.beams(:, 1)));
[az_axis_s, az_ord] = sort(sum_az.az_axis(:));
raz_anchor = zeros(numel(az_anchors), numel(az_grid));
for i = 1:numel(az_anchors)
    b = find(sum_az.beams(:, 1) == az_anchors(i), 1);
    raz_abs = abs(diff_az.data(el0_idx, :, f_idx, b) ./ (sum_az.data(el0_idx, :, f_idx, b) + eps));
    raz_abs = raz_abs(az_ord);  % 按方位角升序重排
    daz = az_axis_s.' - az_anchors(i);
    raz_anchor(i, :) = interp1(daz, raz_abs, az_grid, 'linear', 'extrap');
end

% ---- 提取俯仰锚点曲线（各波位 az=0 切割，按名义俯仰角重定位为 δel）----
az0_idx = find(abs(sum_el.az_axis) < 1e-9, 1);
if isempty(az0_idx), [~, az0_idx] = min(abs(sum_el.az_axis)); end
el_anchors = sort(unique(sum_el.beams(:, 2)));
[el_axis_s, el_ord] = sort(sum_el.el_axis(:));
rel_anchor = zeros(numel(el_anchors), numel(el_grid));
for i = 1:numel(el_anchors)
    b = find(sum_el.beams(:, 2) == el_anchors(i), 1);
    rel_abs = abs(diff_el.data(:, az0_idx, f_idx, b) ./ (sum_el.data(:, az0_idx, f_idx, b) + eps));
    rel_abs = rel_abs(el_ord);  % 按俯仰角升序重排
    del = el_axis_s.' - el_anchors(i);
    rel_anchor(i, :) = interp1(del, rel_abs(:).', el_grid, 'linear', 'extrap');
end

% ---- 波位排布 ----
if isstruct(beam_schedule) && isfield(beam_schedule, 'beam_positions') && ~isempty(beam_schedule.beam_positions)
    beam_positions = beam_schedule.beam_positions;
else
    beam_positions = [0 0];  % 无排布时退回法向单波位
end
num_beams = size(beam_positions, 1);

% ---- 逐波位插值生成 LUT ----
lut_set = cell(1, num_beams);
for b = 1:num_beams
    az_c = beam_positions(b, 1);
    el_c = beam_positions(b, 2);
    raz_lut = interp_anchor_local(az_anchors, raz_anchor, az_c);
    rel_lut = interp_anchor_local(el_anchors, rel_anchor, el_c);
    beam_lut = struct();
    beam_lut.beam_az = az_c;
    beam_lut.beam_el = el_c;
    beam_lut.az_grid = az_grid;
    beam_lut.el_grid = el_grid;
    beam_lut.raz_map = repmat(raz_lut(:).', numel(el_grid), 1);  % [nEl, nAz]
    beam_lut.rel_map = repmat(rel_lut(:), 1, numel(az_grid));     % [nEl, nAz]
    lut_set{b} = beam_lut;
end

monopulse_lut = struct();
monopulse_lut.roi_deg = roi_deg;
% 批次级符号位（把硬件符号位映射到 δ 正方向）。调用方可用 cfg.angle.sign_az/sign_el 覆盖。
monopulse_lut.sign_az = 1;
monopulse_lut.sign_el = 1;
% 判别器相位 φ(°)：δ>0 时 arg(Δ/Σ) 的实测值，调用方可用 cfg.angle.phi_az/phi_el 覆盖。
% 0 = 旧的 sign(real()) 行为；实测本批 φ_az ≈ -113°（100% 同号平台 -135°~-90°）。
monopulse_lut.phi_az = 0;
monopulse_lut.phi_el = 0;
monopulse_lut.data = lut_set;
monopulse_lut.fc_used = fc_used;
monopulse_lut.az_anchors = az_anchors;
monopulse_lut.el_anchors = el_anchors;
monopulse_lut.raz_anchor = raz_anchor;
monopulse_lut.rel_anchor = rel_anchor;
monopulse_lut.source = struct( ...
    'sum_az', sum_az_file, 'diff_az', diff_az_file, ...
    'sum_el', sum_el_file, 'diff_el', diff_el_file);

% ---- 写缓存（下次运行参数/排布不变时直接命中）----
cached_lut = monopulse_lut; %#ok<NASGU>
cache_version = LUT_CACHE_VERSION; %#ok<NASGU>
cache_fc_hz = fc_hz; %#ok<NASGU>
cache_roi_deg = roi_deg; %#ok<NASGU>
cache_step_deg = step_deg; %#ok<NASGU>
cache_beam_positions = beam_positions; %#ok<NASGU>
save(cache_file, 'cached_lut', 'cache_version', 'cache_fc_hz', 'cache_roi_deg', ...
    'cache_step_deg', 'cache_beam_positions');

% ---- 诊断：模值曲线是 V 形，只有单调段可反查，必须报告可用范围 ----
fprintf('[实测LUT] fc=%.0f MHz, %d 波位, ROI=±%.1f°, 栅格 %.2f°（模值 |Δ/Σ|，无符号）\n', ...
    fc_used / 1e6, num_beams, roi_deg, step_deg);
fprintf('[实测LUT]   方位锚点 %s，俯仰锚点 %s\n', mat2str(az_anchors), mat2str(el_anchors));
bad_az = 0;  bad_el = 0;
for b = 1:num_beams
    [rng_az, val_az] = mono_range_local(az_grid, lut_set{b}.raz_map(1, :));
    [rng_el, val_el] = mono_range_local(el_grid, lut_set{b}.rel_map(:, 1).');
    fprintf(['[实测LUT]   波位 az=%+6.2f° el=%+6.2f°: 方位单调至 ±%.2f°(|Δ/Σ|最大 %.4f), ' ...
             '俯仰单调至 ±%.2f°(|Δ/Σ|最大 %.4f)\n'], ...
        beam_positions(b, 1), beam_positions(b, 2), rng_az, val_az, rng_el, val_el);
    bad_az = bad_az + (rng_az < roi_deg - 1e-9);
    bad_el = bad_el + (rng_el < roi_deg - 1e-9);
end
if bad_az > 0 || bad_el > 0
    warning('mono_angle:LutMonotonicShort', ...
        ['有 %d 个波位的方位曲线、%d 个波位的俯仰曲线在 ±%.1f° 内不单调（模值回落后反查有歧义）。' ...
         '这些波位超出其单调范围的点会被钳到单调段端点，建议缩小 ROI 或改用双分支 LUT。'], ...
        bad_az, bad_el, roi_deg);
end
end

% ---------------------------------------------------------------------
function [half_range, val_at] = mono_range_local(grid, curve)
%MONO_RANGE_LOCAL 模值曲线在正半轴上首个单调递增段的 |δ| 上界及其对应模值。
grid = grid(:).';
curve = curve(:).';
sel = grid >= 0;
g = grid(sel);
v = curve(sel);
[g, ord] = sort(g);
v = v(ord);
k = find(diff(v) <= 0, 1, 'first');
if isempty(k), k = numel(g); end
k = max(k, 1);
half_range = g(k);
val_at = v(k);
end

function curve = interp_anchor_local(anchors, anchor_matrix, pos)
%INTERP_ANCHOR_LOCAL 按波位中心角 pos 对锚点曲线矩阵做逐点插值。
% anchors: [nAnchor×1] 升序波位中心角；anchor_matrix: [nAnchor×nPoint] 每行一条锚点曲线。
% 超出锚点范围时钳位到最近锚点（避免外推失控）。
pos_c = min(max(pos, min(anchors)), max(anchors));
if numel(anchors) < 2
    curve = anchor_matrix(1, :);
    return;
end
curve = interp1(anchors, anchor_matrix, pos_c, 'linear');
end

function f = find_ccc_local(d)
%FIND_CCC_LOCAL 返回目录 d 下第一个 .ccc 文件完整路径；无则返回 ''。
d = dir(fullfile(d, '*.ccc'));
if isempty(d)
    f = '';
    return;
end
f = fullfile(d(1).folder, d(1).name);
end
