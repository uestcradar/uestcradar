function varargout = mono_angle(varargin)
%MONO_ANGLE 单脉冲测角统一入口。
%
% === 模式 1：LUT 生成（理论模型）===
%   monopulse_lut = mono_angle('generate_lut', k_az, k_el, roi_deg, step_deg, beam_schedule)
%
% === 模式 1b：LUT 生成（实测方向图）===
%   monopulse_lut = mono_angle('generate_measured_lut', pattern_dir, fc_hz, roi_deg, step_deg, lut_sign)
%
% === 模式 2：LUT 查表测角（2D 解耦）===
%   [r_m, v_m, az_m, el_m, sz_m, angle_stats] = mono_angle( ...
%       r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
%       pwr, az_ratio, el_ratio, ~, angle_r_range, angle_v_range, min_display_power_dB, ...
%       monopulse_lut, beam_id)
%   注意：k_mono 位置（第10参）在 LUT 模式下被忽略，占位即可。LUT 模式下 az_m/el_m 输出角度偏移量（度）。
%   angle_stats(可选第6出参)：本帧偏移超出 ±ROI / 反查失败的点数统计，字段见 angle_by_lut。
%
% === 模式 3：线性 k_mono 测角（回退兼容）===
%   [r_m, v_m, az_m, el_m, sz_m] = mono_angle( ...
%       r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, n_clu, ...
%       pwr, az_ratio, el_ratio, k_mono, angle_r_range, angle_v_range, min_display_power_dB)
%   注意：此模式下 az_m/el_m 输出的是鉴角比值（ratio/k_mono），非物理角度。

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
    pwr, az_ratio, el_ratio, monopulse_lut, beam_id, ...
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
    'max_abs_az_oob', 0);    ...  % 超限点中最大 |方位偏移| (°)

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

    % Step 2: 在 el=el_off 切片上用 raz 查方位偏移
    vec_raz_slice = interp2(lut.az_grid, lut.el_grid, lut.raz_map, ...
                            vec_az, repmat(el_off, size(vec_az)), 'linear');
    az_off = interp1_local(vec_raz_slice(:), vec_az, m_raz);
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
% LUT 生成（本地子函数）
% =====================================================================
function monopulse_lut = generate_lut_local(k_az, k_el, roi_deg, step_deg, beam_schedule)
%GENERATE_LUT_LOCAL 理论模型 LUT 生成。
% 模型：raz = k_az * sin(δaz) * cos(δel)，rel = k_el * sin(δel)
% 方位耦合项 cos(δel) 反映俯仰偏离时有效孔径的缩小。

if nargin < 4 || isempty(step_deg), step_deg = 0.1; end
if nargin < 5, beam_schedule = []; end

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
if ~isempty(beam_schedule) && isfield(beam_schedule, 'num_beams') && beam_schedule.num_beams > 1
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
else
    monopulse_lut.data = {monopulse_lut};
    fprintf('[LUT] 已生成单波位鉴角表（k_az=%.1f, k_el=%.1f, ROI=±%.1f°, 步长 %.1f°）\n', ...
        k_az, k_el, roi_deg, step_deg);
end
end

% =====================================================================
% 实测方向图 LUT 生成（本地子函数）
% =====================================================================
function monopulse_lut = generate_measured_lut_local(pattern_dir, fc_hz, roi_deg, step_deg, lut_sign, beam_schedule)
%GENERATE_MEASURED_LUT_LOCAL 用实测远场方向图生成比幅单脉冲鉴角 LUT（支持逐波位）。
% 鉴角原理：raz = real(Δaz/Σ)，rel = real(Δel/Σ)。
% 单基地雷达下发射方向图在 Δ/Σ 比值中约去，故只需接收方向图。
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
%   lut_sign       - 鉴角曲线符号（可选，默认 1；镜像时置 -1）
%   beam_schedule  - 波位排布（可选），含 beam_positions [nBeam×2]；为空则只生成法向单表
% 输出：
%   monopulse_lut  - roi_deg + data{nBeam}（每波位 az_grid/el_grid/raz_map/rel_map）

if nargin < 5 || isempty(lut_sign), lut_sign = 1; end
if nargin < 6, beam_schedule = []; end

% ---- 缓存：参数不变时复用（含波位排布，排布变了自动失效）----
cache_file = fullfile(pattern_dir, 'measured_lut_cache.mat');
if exist(cache_file, 'file') == 2
    S = load(cache_file);  % 整读缓存（旧版无 cache_beam_positions 时靠 isfield 判空，避免缺字段告警）
    bp_now = [];
    if isstruct(beam_schedule) && isfield(beam_schedule, 'beam_positions')
        bp_now = beam_schedule.beam_positions;
    end
    bp_cmp = isfield(S, 'cache_beam_positions') && isequal(S.cache_beam_positions, bp_now);
    if isfield(S, 'cached_lut') ...
            && isfield(S, 'cache_fc_hz') && abs(S.cache_fc_hz - fc_hz) < 1e-6 ...
            && isfield(S, 'cache_roi_deg') && S.cache_roi_deg == roi_deg ...
            && isfield(S, 'cache_step_deg') && S.cache_step_deg == step_deg ...
            && isfield(S, 'cache_lut_sign') && S.cache_lut_sign == lut_sign ...
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
    raz_abs = real(diff_az.data(el0_idx, :, f_idx, b) ./ (sum_az.data(el0_idx, :, f_idx, b) + eps));
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
    rel_abs = real(diff_el.data(:, az0_idx, f_idx, b) ./ (sum_el.data(:, az0_idx, f_idx, b) + eps));
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
    raz_lut = interp_anchor_local(az_anchors, raz_anchor, az_c) * lut_sign;
    rel_lut = interp_anchor_local(el_anchors, rel_anchor, el_c) * lut_sign;
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
cache_fc_hz = fc_hz; %#ok<NASGU>
cache_roi_deg = roi_deg; %#ok<NASGU>
cache_step_deg = step_deg; %#ok<NASGU>
cache_lut_sign = lut_sign; %#ok<NASGU>
cache_beam_positions = beam_positions; %#ok<NASGU>
save(cache_file, 'cached_lut', 'cache_fc_hz', 'cache_roi_deg', ...
    'cache_step_deg', 'cache_lut_sign', 'cache_beam_positions');

% ---- 诊断 ----
fprintf('[实测LUT] fc=%.0f MHz, %d 波位, ROI=±%.1f°, 栅格 %.2f°, 符号=%d\n', ...
    fc_used / 1e6, num_beams, roi_deg, step_deg, lut_sign);
fprintf('[实测LUT]   方位锚点 %s，俯仰锚点 %s\n', mat2str(az_anchors), mat2str(el_anchors));
sel1 = find(abs(az_grid) <= 1.0);
for b = 1:num_beams
    p_az = polyfit(az_grid(sel1), lut_set{b}.raz_map(1, sel1), 1);
    p_el = polyfit(el_grid(sel1), lut_set{b}.rel_map(sel1, 1)', 1);
    fprintf('[实测LUT]   波位 az=%+6.2f° el=%+6.2f°: k_az=%+.4f, k_el=%+.4f (局部±1°)\n', ...
        beam_positions(b, 1), beam_positions(b, 2), p_az(1), p_el(1));
end
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

% =====================================================================
% 鲁棒一维插值（支持外推）
% =====================================================================
function y = interp1_local(x, v, xq)
x = x(:); v = v(:);
% 按 x 升序重排（实测鉴角曲线可能单调递减，需正确配对 x 与 v 后再插值）
[xs, si] = sort(x);
vs = v(si);
[xu, ia] = unique(xs);
vu = vs(ia);
if length(xu) < 2, y = NaN; return; end
try
    y = interp1(xu, vu, xq, 'linear', 'extrap');
catch
    y = NaN;
end
end
