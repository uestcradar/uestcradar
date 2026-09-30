%% test_sign_selfcheck.m —— 符号自检的合成数据回归
%
%  验证两件事：
%   1. mono_angle 的 angle_stats 里新增的 4 个累加量 + n_sc_pairs 被正确填充；
%   2. sign_selfcheck 能分辨"φ 正确"（eff≈1, ok）与"φ 偏 90°"（eff≈0, fail）。
%
%  合成模型（与 docs/monopulse_sign_failure.md §2 一致）：判别器纯虚，
%  δ>0 时 arg(Δ/Σ) = φ，δ<0 时 arg(Δ/Σ) = φ + 180°；模值 |Δ/Σ| = k·|δ|。
%
%  运行：matlab -batch "test_sign_selfcheck"
clear; clc;
addpath(fullfile(fileparts(mfilename('fullpath')), 'src'));

PHI_TRUE = -93.5;      % 本批实测 φ_az（见 docs/monopulse_sign_failure.md §4）
K_SLOPE  = 0.08;       % |Δ/Σ| 每度
ROI      = 5;          % °
N_TGT    = 60;

% ---- 建一个最小可用的模值 LUT（V 形，谷底在 0）----
grid      = -ROI : 0.5 : ROI;
lut.az_grid = grid;
lut.el_grid = grid;
% 尺寸须为 [numel(el) numel(az)]，与 mono_angle 里 interp2(az_grid, el_grid, raz_map, ...) 一致
lut.rel_map = repmat(K_SLOPE * abs(grid(:)), 1, numel(grid));            % δaz=0 剖面：|Δel/Σ|
lut.raz_map = repmat(K_SLOPE * abs(grid), numel(grid), 1);               % 全二维：|Δaz/Σ|
lut.roi_deg = ROI;
lut.phi_az  = PHI_TRUE;
lut.phi_el  = PHI_TRUE;
lut.sign_az = 1;
lut.sign_el = 1;

% ---- 合成 N_TGT 个目标，各占一个距离-速度格、各自成簇 ----
nR = 10; nV = 8;
r_disp = (1:nR)';  v_disp = (1:nV)';
pwr       = zeros(nR, nV);
az_ratio  = complex(zeros(nR, nV));
el_ratio  = complex(zeros(nR, nV));
det_r_idx = zeros(N_TGT, 1);  det_v_idx = zeros(N_TGT, 1);
DA = zeros(N_TGT, 1);  DE = zeros(N_TGT, 1);

rng(7);                                    % 固定种子，可复现
for n = 1:N_TGT
    ri = mod(n - 1, nR) + 1;
    vi = mod(floor((n - 1) / nR), nV) + 1;
    da = -3 + 6 * rand();                  % δaz ∈ (−3, 3)
    de = -3 + 6 * rand();
    DA(n) = da;  DE(n) = de;
    pwr(ri, vi)      = 1 + n;
    det_r_idx(n)     = ri;  det_v_idx(n) = vi;
    az_ratio(ri, vi) = K_SLOPE * abs(da) * exp(1i * deg2rad(PHI_TRUE + 180 * (da < 0)));
    el_ratio(ri, vi) = K_SLOPE * abs(de) * exp(1i * deg2rad(PHI_TRUE + 180 * (de < 0)));
end
clu_ids = (1:N_TGT)';

fprintf('\n合成目标 %d 个（δaz/δel 各 ∈ (−3,3)°，φ_true = %.1f°）\n\n', N_TGT, PHI_TRUE);

% ---- 用例 ----
cases = { ...
    'φ 正确',                  PHI_TRUE,           'ok'; ...
    'φ 偏 45°',                PHI_TRUE + 45,      'warn'; ...
    'φ 偏 90°（0916 失效情形）', PHI_TRUE + 90,      'fail'; ...
    'φ = 0（旧行为）',           0,                  'fail' };

n_fail = 0;
for c = 1:size(cases, 1)
    name = cases{c, 1};
    lut.phi_az = cases{c, 2};
    lut.phi_el = cases{c, 2};

    [~, ~, ~, ~, ~, st] = mono_angle( ...
        r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, N_TGT, ...
        pwr, az_ratio, el_ratio, [], ...
        [-inf inf], [-inf inf], -inf, lut, 1);

    sc = sign_selfcheck(st);
    ok_n  = (st.n_sc_pairs == N_TGT);
    ok_v  = strcmp(sc.verdict_az, cases{c, 3});
    if ~ok_n || ~ok_v, n_fail = n_fail + 1; end

    fprintf('%-22s → eff_az=%.3f eff_el=%.3f  判定=%s(期望 %s)  n_sc_pairs=%d/%d  %s\n', ...
        name, sc.eff_az, sc.eff_el, sc.verdict_az, cases{c, 3}, ...
        st.n_sc_pairs, N_TGT, ternary(ok_n && ok_v, 'PASS', 'FAIL'));
end

% ---- 单调性：eff 应随 |Δφ| 单调不增 ----
phi_sweep = PHI_TRUE + (0:5:90);
eff_sweep = zeros(size(phi_sweep));
for i = 1:numel(phi_sweep)
    lut.phi_az = phi_sweep(i);  lut.phi_el = phi_sweep(i);
    [~, ~, ~, ~, ~, st] = mono_angle( ...
        r_disp, v_disp, det_r_idx, det_v_idx, clu_ids, N_TGT, ...
        pwr, az_ratio, el_ratio, [], ...
        [-inf inf], [-inf inf], -inf, lut, 1);
    eff_sweep(i) = sign_selfcheck(st).eff_az;
end
mono_ok = all(diff(eff_sweep) <= 1e-12);
if ~mono_ok, n_fail = n_fail + 1; end
fprintf('\n|Δφ| 0→90° 时 eff 单调不增: %s\n   ', ternary(mono_ok, 'PASS', 'FAIL'));
fprintf('%.2f ', eff_sweep);  fprintf('\n');

% ---- 反推的 |Δφ| 应等于真实 |Δφ|（±5° 容差，受相位离散度影响）----
dphi_err = max(abs(acosd(eff_sweep) - (0:5:90)));
fprintf('由 eff 反推的 |Δφ| 最大偏差: %.1f°  %s\n', dphi_err, ternary(dphi_err <= 5, 'PASS', 'FAIL'));
if dphi_err > 5, n_fail = n_fail + 1; end

fprintf('\n');
if n_fail == 0
    fprintf('===== 全部通过 =====\n');
else
    error('test_sign_selfcheck: %d 项未通过', n_fail);
end

function s = ternary(c, a, b)
if c, s = a; else, s = b; end
end
