function out = estimate_phi_from_dw(channel_files, channel_vars, tx, pri_len, dw_bin, opts)
%ESTIMATE_PHI_FROM_DW 用直达波在线测判别器相位 φ（每次运行零成本）。
%
% 原理（推导见 docs/monopulse_sign_failure.md §7.1）
%   直达波是 TX 数字域到 RX 数字域的内部漏泄，位置由硬件流水延迟决定，在每个波位、
%   每个 PRI 里完全相同，且逐通道进入各自的 RF 链路。令 Ψ = 注入点处"差口耦合/和口
%   耦合"的相位差，则
%       arg(R_dw) = Ψ + φ_chain = Ψ + (φ − 90°)   ⟹   φ = arg(R_dw) + (90° − Ψ)
%   把括号里的常数一次性标定成 phi_correction，运行时只要读直达波的复比值即可：
%       phi_az = wrap180(arg(R_dw_az) + phi_correction_az)
%
%   **存进配置的就是代码里相加的那个数**，正负号没有第二种解释（不要再引入含糊的 K）。
%
% ⚠ 状态：**已从批处理管线移除，请勿在管线里接线**（2026-09-30）
%   本模块与 test_estimate_phi_from_dw.m 保留备查，需要时可手动调用复核。
%
%   移除原因：原设计假设"φ 的任何漂移都会 1:1 反映在 arg(R_dw) 上"，实测推翻了它。
%   相隔 12 天的两次真值反查（0916 / 0928）给出 φ_az ≈ -93.5° 与 ≈ -104.6°（相距 11°），
%   而同期 arg(R_dw)_az 从 -138.8° 变到 -42.9°（**96°**）—— 即 **φ 稳、直达波不稳**，
%   二者解耦。后果是 phi_correction 必须每次会话重标，而一旦用过期值，本模块会**自信地
%   输出一个错 φ**：0928 数据上按旧补偿量算出的 φ = +2.1°，同号率仅 14.5%（近乎全错）。
%
%   判据：判符号正确的窗口 |Δφ| < 90°（宽 180°），而观测到的 φ 漂移是 11° —— 容差是
%   观测漂移的 8 倍。所以正确做法是**把 φ 固定成常量**（cfg.angle.phi_az），而不是
%   用直达波在线测：前者拿稳定量，后者拿不稳定量。
%
% 有效性边界（即便手动调用也要记住）
%   该映射只在漂移发生在**漏泄注入点下游**时成立；且 0916→0928 的实测说明注入点上游的
%   漂移**确实会发生且幅度很大**（96°）。因此任何使用都必须配 sign_selfcheck.m 做独立校验，
%   本函数里的"与配置值偏差过大"告警只是辅助，不能替代它。
%   详见 docs/monopulse_sign_failure.md §7.4b / §7.5。
%
% 输入
%   channel_files - 三个通道的 flat .mat 路径元胞 {ch0, ch1, ch2}
%   channel_vars  - 三个通道的变量名元胞，如 {'ch0','ch1','ch2'}
%   tx            - TX 参考波形结构体（用 tx.data 做匹配滤波）
%   pri_len       - 单个 PRI 的采样点数
%   dw_bin        - 直达波距离门（**1 基**，取自 preprocess 的 shared_preproc.dw_bin）
%   opts          - 配置结构体：
%       .phi_correction_az/.phi_correction_el  (必填, °) 一次性标定的补偿量
%       .dw_n_dwell        驻留数（默认 40）
%       .dw_pri_per_dwell  每驻留 PRI 数（默认 64；直达波逐脉冲完全一致，
%                          相干平均只为提 SNR，256 与 64 的相位结果差异在噪声内）
%       .dw_start_pri      起始 PRI（默认 1）；传 beam_schedule.initial_scan_pri 可避开启动瞬态
%       .dw_phase_iqr_max_deg  驻留间相位四分位距上限（默认 20°）
%       .dw_pnr_min        直达波峰/噪比下限（默认 6）
%       .phi_az/.phi_el    仅用于对照告警的静态配置值（可选）
%       .dev_warn_deg      与静态值偏差告警门限（默认 60°）
%       .status_cb         状态输出函数（可选）
%
% 输出 out
%   .phi_az/.phi_el        本次运行应采用的 φ（°）
%   .arg_dw_az/.arg_dw_el  arg(R_dw)（°）
%   .abs_dw_az/.abs_dw_el  |R_dw|
%   .iqr_az/.iqr_el        驻留间相位四分位距（°）
%   .pnr                   直达波峰/噪比
%   .n_dwell               实际驻留数
%   .ok                    质量门限是否通过
%   .msg                   单行中文结论
%
% 用法
%   dw = estimate_phi_from_dw(parse_bundle.rx_channel_files, ...
%            parse_bundle.channel_var_names, parse_bundle.tx, rd_ctx.pri_len, ...
%            shared_preproc.dw_bin, cfg.angle);
%   monopulse_lut.phi_az = dw.phi_az;  monopulse_lut.phi_el = dw.phi_el;

% ---- 参数与默认值 ----
o = struct('phi_correction_az', [], 'phi_correction_el', [], ...
           'dw_n_dwell', 40, 'dw_pri_per_dwell', 64, 'dw_start_pri', 1, ...
           'dw_phase_iqr_max_deg', 20, 'dw_pnr_min', 6, ...
           'phi_az', [], 'phi_el', [], 'dev_warn_deg', 60, 'status_cb', []);
if nargin >= 6 && ~isempty(opts)
    f = fieldnames(opts);
    for i = 1:numel(f)
        if isfield(o, f{i}), o.(f{i}) = opts.(f{i}); end
    end
end
if isempty(o.status_cb)
    o.status_cb = @(msg) fprintf('%s\n', msg);
end
if isempty(o.phi_correction_az) || isempty(o.phi_correction_el)
    error('estimate_phi_from_dw:MissingCorrection', ...
        ['必须提供 phi_correction_az / phi_correction_el（一次性标定的直达波补偿量）。\n' ...
         '它们不是 0：0916 实测 +45°(方位) / +49°(俯仰，暂定)，见 docs/monopulse_sign_failure.md §7.1。\n' ...
         '注意该补偿量是"每次会话"的量（§7.4b 实测 12 天内变了 96°），换会话必须重标。']);
end
if numel(channel_files) < 3 || numel(channel_vars) < 3
    error('estimate_phi_from_dw:NeedThreeChannels', '需要三个通道（ch0/ch1/ch2）才能算 Δ/Σ。');
end

n_dwell = max(1, round(o.dw_n_dwell));
dpri    = max(1, round(o.dw_pri_per_dwell));

% ---- 打开三个通道（flat .mat，变量在顶层）----
m = cell(1, 3);
for ch = 1:3
    if exist(channel_files{ch}, 'file') ~= 2
        error('estimate_phi_from_dw:MissingFile', '缺通道文件：%s', channel_files{ch});
    end
    m{ch} = matfile(channel_files{ch});
end

% 通道数据总 PRI 数（从 ch0 变量尺寸反推）
w = whos('-file', channel_files{1});
w = w(strcmp({w.name}, channel_vars{1}));
if isempty(w)
    error('estimate_phi_from_dw:MissingVar', ...
        '文件 %s 中没有变量 %s', channel_files{1}, channel_vars{1});
end
total_pri = floor(double(w.size(1)) / pri_len);

% ---- 匹配滤波参考（与 preprocess.m 的 rd_ctx.conj_ref_freq 一致）----
conj_ref_freq = conj(fft(single(tx.data), pri_len)) .* single(hamming(pri_len));

% ---- 均匀撒驻留，跨多个波位 ----
start_base = max(1, round(o.dw_start_pri));
span = total_pri - dpri - 1 - start_base;
if span < 0, span = 0; end
starts = round(start_base + (0:n_dwell-1) * span / max(n_dwell - 1, 1));
starts = min(max(starts, 1), max(1, total_pri - dpri));

acc  = complex(zeros(1, 3));       % 跨驻留相干累加（直达波逐驻留同相）
rdw  = complex(zeros(n_dwell, 2)); % 逐驻留比值：(:,1)=ch2/ch0(方位), (:,2)=ch1/ch0(俯仰)
pnr  = zeros(n_dwell, 1);
for di = 1:n_dwell
    samp0 = (starts(di) - 1) * pri_len + 1;
    dup = complex(zeros(1, 3));
    for ch = 1:3
        rx = reshape(double(m{ch}.(channel_vars{ch})(samp0 : samp0 + pri_len * dpri - 1, 1)), ...
                     pri_len, dpri);
        rx = rx - mean(rx, 1);                                    % 快时间去均值，与 preprocess 一致
        pc = ifft(bsxfun(@times, fft(rx, pri_len, 1), conj_ref_freq), pri_len, 1);
        dup(ch) = mean(pc(dw_bin, :));                            % 驻留内脉冲相干平均
        if ch == 1
            prof = mean(abs(pc), 2);                              % 驻留内的平均距离像
        end
    end
    acc = acc + dup;
    rdw(di, 1) = dup(3) / (dup(1) + eps);    % ch2/ch0 = 方位差/和
    rdw(di, 2) = dup(2) / (dup(1) + eps);    % ch1/ch0 = 俯仰差/和
    pnr(di)     = prof(dw_bin) / (median(prof) + eps);
end

% ---- 汇总比值与质量 ----
r_dw_az = acc(3) / (acc(1) + eps);
r_dw_el = acc(2) / (acc(1) + eps);
arg_az  = angle(r_dw_az) * 180 / pi;
arg_el  = angle(r_dw_el) * 180 / pi;

% 驻留间相位散布：逐驻留相位已含 180° 模糊以外的全部噪声，用 IQR（抗离群）
pa = angle(exp(1i * (angle(rdw(:, 1)) - angle(r_dw_az))));   % 相对汇总相位
pe = angle(exp(1i * (angle(rdw(:, 2)) - angle(r_dw_el))));
iqr_az = iqr_(pa) * 180 / pi;
iqr_el = iqr_(pe) * 180 / pi;
pnr_med = median(pnr);

out = struct();
out.arg_dw_az = arg_az;   out.arg_dw_el = arg_el;
out.abs_dw_az = abs(r_dw_az);  out.abs_dw_el = abs(r_dw_el);
out.iqr_az = iqr_az;      out.iqr_el = iqr_el;
out.pnr = pnr_med;
out.n_dwell = n_dwell;
out.phi_az = wrap180(arg_az + o.phi_correction_az);
out.phi_el = wrap180(arg_el + o.phi_correction_el);

% ---- 质量门限：不合格则判为不可用（调用方应回退到静态 φ）----
out.ok = (pnr_med >= o.dw_pnr_min) && (iqr_az <= o.dw_phase_iqr_max_deg) ...
         && (iqr_el <= o.dw_phase_iqr_max_deg);

% ---- 与静态配置值的偏差：这是"跟踪在工作"还是"映射失效"的区分点 ----
dev_txt = '';
if ~isempty(o.phi_az) && ~isempty(o.phi_el)
    d_az = abs(wrap180(out.phi_az - o.phi_az));
    d_el = abs(wrap180(out.phi_el - o.phi_el));
    if max(d_az, d_el) > o.dev_warn_deg
        dev_txt = sprintf('  | 与配置值偏差 方位%.0f°/俯仰%.0f° > %g° —— 若这是馈线/通道变更所致，属正常跟踪；否则请查 sign_selfcheck', ...
            d_az, d_el, o.dev_warn_deg);
    end
end

out.msg = sprintf(['[直达波φ] arg(R_dw) 方位=%+.1f°(%.2f) 俯仰=%+.1f°(%.2f)', ...
                   ' | 驻留间IQR 方位%.1f°/俯仰%.1f° | 峰噪比%.1f | φ_az=%.1f° φ_el=%.1f° | %s%s'], ...
    arg_az, abs(r_dw_az), arg_el, abs(r_dw_el), ...
    iqr_az, iqr_el, pnr_med, out.phi_az, out.phi_el, ...
    ternary_(out.ok, '可用', '不可用(峰噪比/散布超限)'), dev_txt);
end

% ---------------------------------------------------------------------
function v = iqr_(x)
q = quantile(x, [0.25 0.75]);
v = q(2) - q(1);
end

function s = ternary_(c, a, b)
if c, s = a; else, s = b; end
end
