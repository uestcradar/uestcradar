function varargout = mono_angle(varargin)
%MONO_ANGLE 单脉冲测角统一入口。
%
% === 模式 1：LUT 生成 ===
%   monopulse_lut = mono_angle('generate_lut', k_az, k_el, roi_deg, step_deg, beam_schedule)
%
% === 模式 2：LUT 查表测角（2D 解耦）===
%   [r_m, v_m, az_m, el_m, sz_m] = mono_angle( ...
%       r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
%       pwr, az_ratio, el_ratio, ~, angle_r_range, angle_v_range, min_display_power_dB, ...
%       monopulse_lut, beam_id)
%   注意：k_mono 位置（第10参）在 LUT 模式下被忽略，占位即可。LUT 模式下 az_m/el_m 输出角度偏移量（度）。
%
% === 模式 3：线性 k_mono 测角（回退兼容）===
%   [r_m, v_m, az_m, el_m, sz_m] = mono_angle( ...
%       r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
%       pwr, az_ratio, el_ratio, k_mono, angle_r_range, angle_v_range, min_display_power_dB)
%   注意：此模式下 az_m/el_m 输出的是鉴角比值（ratio/k_mono），非物理角度。

% ---- 模式分发 ----
if ischar(varargin{1}) && strcmp(varargin{1}, 'generate_lut')
    % === LUT 生成模式 ===
    varargout{1} = generate_lut_local(varargin{2:end});
    return;
end

% === 测角模式 ===
[r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
    pwr, az_ratio, el_ratio, k_mono, angle_r_range, angle_v_range, min_display_power_dB] = ...
    deal(varargin{1:13});

% 检测是否有 LUT 参数
use_lut = (nargin >= 14) && isstruct(varargin{14}) && ~isempty(varargin{14});
if use_lut
    monopulse_lut = varargin{14};
    beam_id = 1;
    if nargin >= 15
        beam_id = varargin{15};
    end
    [r_m, v_m, az_m, el_m, sz_m] = angle_by_lut( ...
        r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
        pwr, az_ratio, el_ratio, monopulse_lut, beam_id, ...
        angle_r_range, angle_v_range, min_display_power_dB);
else
    [r_m, v_m, az_m, el_m, sz_m] = angle_by_kmono( ...
        r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
        pwr, az_ratio, el_ratio, k_mono, ...
        angle_r_range, angle_v_range, min_display_power_dB);
end

varargout = {r_m, v_m, az_m, el_m, sz_m};
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
function [r_m, v_m, az_m, el_m, sz_m] = angle_by_lut( ...
    r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
    pwr, az_ratio, el_ratio, monopulse_lut, beam_id, ...
    angle_r_range, angle_v_range, min_display_power_dB)

r_m = [];
v_m = [];
az_m = [];
el_m = [];
sz_m = [];

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

% 预提取 δaz=0 剖面（Step 1 用）
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

    m_raz = double(az_ratio(rb, vb));
    m_rel = double(el_ratio(rb, vb));

    % Step 1: 在 az=0 剖面用 rel 查俯仰偏移
    el_off = interp1_local(vec_rel_az0, vec_el, m_rel);
    if isnan(el_off) || abs(el_off) > roi_deg, continue; end
    el_off = double(el_off);

    % Step 2: 在 el=el_off 切片上用 raz 查方位偏移
    vec_raz_slice = interp2(lut.az_grid, lut.el_grid, lut.raz_map, ...
                            vec_az, repmat(el_off, size(vec_az)), 'linear');
    az_off = interp1_local(vec_raz_slice(:), vec_az, m_raz);
    if isnan(az_off) || abs(az_off) > roi_deg, continue; end

    peak_pwr_dB = 10 * log10(double(pwr_ci(best)) + eps);
    if peak_pwr_dB < min_display_power_dB, continue; end

    r_m(end + 1) = r_val; %#ok<AGROW>
    v_m(end + 1) = v_val; %#ok<AGROW>
    az_m(end + 1) = az_off; %#ok<AGROW>
    el_m(end + 1) = el_off; %#ok<AGROW>
    sz_m(end + 1) = max(30, peak_pwr_dB - min_display_power_dB + 10); %#ok<AGROW>
end
end

% =====================================================================
% LUT 生成（本地子函数）
% =====================================================================
function monopulse_lut = generate_lut_local(k_az, k_el, roi_deg, step_deg, beam_schedule)
%GENERATE_LUT_LOCAL 理论模型 LUT 生成。
% 模型：raz = k_az * sin(δaz) * cos(δel + el_center)，rel = k_el * (sin(δel + el_center) - sin(el_center))
% 为每个波位考虑波束中心俯仰对耦合项的影响。

if nargin < 4 || isempty(step_deg), step_deg = 0.1; end
if nargin < 5 || isempty(beam_schedule)
    error('mono_angle:MissingBeamSchedule', 'generate_lut 模式需要 beam_schedule 参数。');
end

k_az = double(k_az);
k_el = double(k_el);
roi_deg = double(roi_deg);

angle_offsets = -roi_deg : step_deg : roi_deg;
[AZ_OFF, EL_OFF] = meshgrid(angle_offsets, angle_offsets);

az_rad = deg2rad(AZ_OFF(:));
el_rad = deg2rad(EL_OFF(:));

raz_vals = k_az * sin(az_rad) .* cos(el_rad);
rel_vals = k_el * sin(el_rad);

monopulse_lut = struct();
monopulse_lut.az_grid = angle_offsets;
monopulse_lut.el_grid = angle_offsets;
monopulse_lut.raz_map = reshape(raz_vals, size(AZ_OFF));
monopulse_lut.rel_map = reshape(rel_vals, size(EL_OFF));
monopulse_lut.k_az = k_az;
monopulse_lut.k_el = k_el;
monopulse_lut.roi_deg = roi_deg;

% 为每个波位生成独立 LUT（考虑波束中心俯仰对耦合项的影响）
num_beams = beam_schedule.num_beams;
lut_set = cell(1, num_beams);
for b = 1:num_beams
    beam_el_c = deg2rad(beam_schedule.beam_positions(b, 2));
    el_r_b = el_rad + beam_el_c;
    raz_b = k_az * sin(az_rad) .* cos(el_r_b);
    rel_b = k_el * (sin(el_r_b) - sin(beam_el_c));

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
fprintf('[LUT] 已为 %d 个波位生成独立鉴角表（k_az=%.1f, k_el=%.1f, ROI=±%.1f°）\n', ...
    num_beams, k_az, k_el, roi_deg);
end

% =====================================================================
% 鲁棒一维插值（支持外推）
% =====================================================================
function y = interp1_local(x, v, xq)
x = x(:); v = v(:);
[xu, idx] = unique(sort(x));
vu = v(idx);
if length(xu) < 2, y = NaN; return; end
try
    y = interp1(xu, vu, xq, 'linear', 'extrap');
catch
    y = NaN;
end
end
