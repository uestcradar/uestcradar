%% test_estimate_phi_from_dw.m —— 直达波在线测 φ 的合成数据回归
%
%  用已知的逐通道直达波复增益造三个通道的 flat .mat，验证：
%   1. 能正确恢复 arg(R_dw)（方位 = ch2/ch0、俯仰 = ch1/ch0）；
%   2. phi = wrap180(arg(R_dw) + phi_correction) 的符号约定正确；
%   3. 质量门限：bin 取错 → 峰噪比掉下去 → ok=false（调用方据此回退静态 φ）；
%   4. 与静态值偏差告警只告警、不改变取值。
%
%  运行：matlab -batch "test_estimate_phi_from_dw"
clear; clc;
addpath(fullfile(fileparts(mfilename('fullpath')), 'src'));

PRI_LEN   = 128;
TOTAL_PRI = 160;                 % 只测逻辑，不与生产规模比（真实规模已单独验证过）
NCHIRP    = 64;
N_DWELL   = 4;                   % 小样本即可：直达波是确定性的
DPRI      = 8;
DW_BIN    = 40;                  % 直达波 bin（1 基）；需满足 (DW_BIN-1)+NCHIRP ≤ PRI_LEN
fs = 30.72e6;  B = 30e6;  tau = 2.08333e-06;
Ksl = B / tau;
tt  = ((0:NCHIRP-1)' - (NCHIRP-1)/2) / fs;
ref = exp(1i*pi*Ksl*tt.^2);      % 与 estimate_phi_from_dw 的 tx.data 同源

% 逐通道复增益：ch0=和, ch1=俯仰差, ch2=方位差
ARG_EL = -144;  ARG_AZ = -139;
g = [1, 0.32*exp(1i*deg2rad(ARG_EL)), 0.70*exp(1i*deg2rad(ARG_AZ))];

% ---- 造数据：每个 PRI 在固定偏移处放一份 ref（确定性 ⇒ 直达波）----
s0 = DW_BIN - 1;                 % 0 基起始样本，匹配滤波峰落在 dw_bin
rng(11);
x = complex(zeros(PRI_LEN*TOTAL_PRI, 3));
for p = 0:TOTAL_PRI-1
    seg = (p*PRI_LEN + s0) + (1:NCHIRP).';
    for ch = 1:3
        x(seg, ch) = g(ch) * ref;
    end
end
x = x + 0.02 * (randn(size(x)) + 1i*randn(size(x)));   % 小噪声，峰噪比充足

tmp = fullfile(tempdir, 'test_dw_phi');
if ~exist(tmp, 'dir'), mkdir(tmp); end
files = cell(1,3);  vars = {'ch0','ch1','ch2'};
for ch = 1:3
    files{ch} = fullfile(tmp, sprintf('syn_ch%d.mat', ch-1));
    S = struct();
    S.(vars{ch}) = x(:, ch);
    % -nocompression：与采集端写的 v7.3 一致。压缩的 HDF5 数据集做部分读取会整块解压，
    % 会把本测试拖到十几分钟（实测踩过），而生产文件不是压缩的。
    save(files{ch}, '-struct', 'S', vars{ch}, '-v7.3', '-nocompression');
end

tx = struct('data', ref);
opts = struct('phi_correction_az', 45, 'phi_correction_el', 49, ...
              'phi_az', -113, 'phi_el', 0, ...        % 静态值：俯仰为 0 = 未标定
              'dw_n_dwell', N_DWELL, 'dw_pri_per_dwell', DPRI, 'dw_start_pri', 1, ...
              'status_cb', @(m) []);

n_fail = 0;
chk = @(c, name, detail) report(c, name, detail);

% ---- 用例 1：正常 ----
out = estimate_phi_from_dw(files, vars, tx, PRI_LEN, DW_BIN, opts);
fprintf('\n%s\n', out.msg);

n_fail = n_fail + chk(abs(out.arg_dw_az - ARG_AZ) < 1.0, '恢复 arg(R_dw)_az', ...
    sprintf('实测 %+.2f° / 期望 %+.0f°', out.arg_dw_az, ARG_AZ));
n_fail = n_fail + chk(abs(out.arg_dw_el - ARG_EL) < 1.0, '恢复 arg(R_dw)_el', ...
    sprintf('实测 %+.2f° / 期望 %+.0f°', out.arg_dw_el, ARG_EL));
n_fail = n_fail + chk(abs(out.abs_dw_az - 0.70) < 0.02, '恢复 |R_dw|_az', ...
    sprintf('实测 %.3f / 期望 0.700', out.abs_dw_az));

% 符号约定：phi = wrap180(arg + correction)
exp_az = wrap180(ARG_AZ + 45);
exp_el = wrap180(ARG_EL + 49);
n_fail = n_fail + chk(abs(out.phi_az - exp_az) < 1.0, 'phi_az = arg + correction', ...
    sprintf('实测 %.1f° / 期望 %.1f°（减号会得到 %.1f°）', out.phi_az, exp_az, wrap180(ARG_AZ - 45)));
n_fail = n_fail + chk(abs(out.phi_el - exp_el) < 1.0, 'phi_el = arg + correction', ...
    sprintf('实测 %.1f° / 期望 %.1f°', out.phi_el, exp_el));

n_fail = n_fail + chk(out.ok, '质量门限判定为可用', sprintf('ok=%d pnr=%.1f', out.ok, out.pnr));
n_fail = n_fail + chk(contains(out.msg, '偏差'), '俯仰偏差告警已触发（静态 phi_el=0 未标定）', '');
n_fail = n_fail + chk(abs(out.phi_el - exp_el) < 1.0, '偏差告警不改变取值', ...
    sprintf('仍取实测 %.1f°（而非静态 0）', out.phi_el));

% ---- 用例 2：dw_bin 取错 → 峰噪比掉下去 → 不可用 ----
out2 = estimate_phi_from_dw(files, vars, tx, PRI_LEN, 110, opts);
n_fail = n_fail + chk(~out2.ok, 'bin 取错时判为不可用', ...
    sprintf('ok=%d pnr=%.2f（下限 %g）', out2.ok, out2.pnr, 6));

% ---- 用例 3：缺 phi_correction 必须报错（防止未标定就启用）----
threw = false;
try
    estimate_phi_from_dw(files, vars, tx, PRI_LEN, DW_BIN, struct('phi_az', -113));
catch
    threw = true;
end
n_fail = n_fail + chk(threw, '缺 phi_correction 时报错', '');

fprintf('\n');
if n_fail == 0
    fprintf('===== 全部通过 =====\n');
else
    error('test_estimate_phi_from_dw: %d 项未通过', n_fail);
end

function n = report(c, name, detail)
if c
    fprintf('  PASS  %s  %s\n', name, detail);
    n = 0;
else
    fprintf('  FAIL  %s  %s\n', name, detail);
    n = 1;
end
end

function y = wrap180(x)
y = mod(x + 180, 360) - 180;
end
