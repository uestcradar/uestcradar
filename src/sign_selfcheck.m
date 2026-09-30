function out = sign_selfcheck(stats_total, n_min)
%SIGN_SELFCHECK 判别器相位 φ 的零成本自检：当前 φ 所在的轴是否还携带侧别信息
%
% 原理
%   单脉冲判别器几乎纯虚（见 mono_angle.m 头注释）：目标簇上 c = Δ/Σ 的相位，
%   δ>0 时聚集在 φ、δ<0 时聚集在 φ+180°。**侧别只做 180° 翻转**，所以把 c 投影到
%   自己认定的轴上，得到的是一个与侧别无关的量：
%
%       |Re(c·exp(-1i·φ_used))| / |c|  =  |cos(Δφ)|,   Δφ = φ_used - φ
%
%   φ_used 落在判别器轴上（Δφ→0）时该比值趋近 1；偏 90° 时趋近 0。
%   注意它**不是**一个随机量：只有相位本身均匀分布（杂波、纯噪声）才会得到
%   E|cosθ| = 2/π ≈ 0.637 这个地板。所以：
%
%       ≈1      φ 正确，符号可信
%       ≈0.637  轴向与相位无关 —— 等价于"符号是抛硬币"
%       ≪0.637  轴向近乎与判别器正交 —— 符号被一个固定偏置主导（0916 实测 ≈0.06）
%
%   这正是 0916 那批数据（φ_used=0）"符号恒为 −1"的直接度量，且不需要真值、
%   不需要标定航线、不需要额外数据 —— 只要把 mono_angle 已有的目标格喂进来。
%
% 它不做什么
%   不做门限、不剔除、不回落、不改测角结果，只输出一个结论供人判断。
%   因此多目标场景下不引入任何新行为（自检量与目标数无关，只随目标格数平均）。
%
% 为什么必须保留（2026-09-30 起它是唯一的兜底）
%   现行口径是"φ 固定成常量 + 每批跑自检"（见 apps/run_batch_pipeline.m 中 cfg.angle.phi_az
%   处的依据）。φ 之所以能固定，是因为它的容差 90° 是观测漂移（12 天 11°）的 8 倍；
%   但**固定常量出错时不会自己报警**，出错与正常长得一模一样。所以本自检不是可选项：
%   去掉它，固定常量就退化成一场赌博。判读口径：
%       ok   ≥0.90（|Δφ| ≲ 26°） 正常
%       warn 0.55~0.90            量程已损失，计划重标
%       fail <0.55                本批符号不可用，立即重标
%
% 输入
%   stats_total - mono_angle 的 angle_stats 跨帧累加结果，需含字段
%                 proj_az_sum / abs_az_sum / proj_el_sum / abs_el_sum / n_sc_pairs
%   n_min       - 给出判定所需的最少目标格数，默认 30（不足则 verdict 为 'na'）
%
% 输出 out
%   .eff_az/.eff_el       投影效率 ∈ [0,1]
%   .dphi_az/.dphi_el     由效率反推的 |Δφ| 估计(°)，仅在 eff ≤ 1 时有效
%   .verdict_az/.verdict_el  'ok' | 'warn' | 'fail' | 'na'
%   .n_pairs              参与累加的目标格数
%   .msg                  单行中文结论（可直接打印）
%
% 用法（在整批数据跑完后）
%   sc = sign_selfcheck(angle_stat_total);
%   lg(sc.msg);
%
% 阈值依据：直接取 |cos Δφ| 的语义，不引入额外假设。
% 翻号窗口是 φ 两侧各 90°，所以 eff 同时编码了"距窗口边缘还有多少裕量"：
%   ok     eff ≥ 0.90  ⟺ |Δφ| ≤ 26° ⟺ 距翻号边界余 ≥ 64°
%   warn   0.55~0.90   ⟺ |Δφ| 26°~57° ⟺ 余 33°~64°，量程已损失 10%~45%
%   fail   eff < 0.55  ⟺ |Δφ| > 57° ⟺ 余 < 33°，且已跌到抛硬币地板(0.637)之下
% 只分三档是刻意的：warn 与 fail 各有单一且可操作的处置（重标 / 立即重标），
% 再细分档位没有对应的不同动作。
%
% **它衡量的是裕量，不是"当前符号对不对"**
%   c 的相位有真实离散度，且 cos 在峰值附近是平的，所以 eff 的 "ok" 带比
%   "同号率 100%" 的平台宽得多（实测：eff≥0.90 覆盖 φ ∈ [−108°,−68°]，宽 40°；
%   同号率 100% 的平台只有 [−98°,−88°]，宽 10°）。因此
%       eff ≥ 0.90 ⟹ |Δφ| ≤ 26° ⟹ 距 90° 的翻号边界还有 ≥64° 裕量
%   是一条**必要条件式的裕量监测**，不能读成"同号率 = eff"。反过来它是成立的：
%   eff 掉进 warn/fail，符号一定不可信 —— 这正是我们要的报警方向。
%
% 已知局限
%   - 假设累加的目标格里以真目标为主。杂波簇相位近似均匀，会把 eff 往 0.637
%     地板拉，所以杂波占比高时 eff 系统性偏低，只能同口径横向比较（换批次/换 φ）。
%   - 有效样本靠 n_sc_pairs；目标格很少时（n_min）不给结论。
%   - 只覆盖方位轴或俯仰轴各自；某一轴目标从未跨过波束中心时，该轴反映的仍是
%     裕量而非该轴实测的侧别正确性。
%
% 见 docs/monopulse_sign_failure.md 与 [[monopulse-modulus-sign-design]]。

if nargin < 2 || isempty(n_min)
    n_min = 30;
end

need = {'proj_az_sum', 'abs_az_sum', 'proj_el_sum', 'abs_el_sum', 'n_sc_pairs'};
for i = 1:numel(need)
    if ~isfield(stats_total, need{i})
        error('sign_selfcheck: 缺少字段 %s（mono_angle 的 angle_stats 是否已含自检累加？）', need{i});
    end
end

n_pairs = double(stats_total.n_sc_pairs);
out = struct('eff_az', NaN, 'eff_el', NaN, ...
             'dphi_az', NaN, 'dphi_el', NaN, ...
             'verdict_az', 'na', 'verdict_el', 'na', ...
             'n_pairs', n_pairs, 'msg', '');

if n_pairs < n_min
    out.msg = sprintf('[符号自检] 样本不足（目标格 %d < %d），跳过。', n_pairs, n_min);
    return;
end

out.eff_az  = safe_eff(stats_total.proj_az_sum, stats_total.abs_az_sum);
out.eff_el  = safe_eff(stats_total.proj_el_sum, stats_total.abs_el_sum);
[out.verdict_az, out.dphi_az] = judge(out.eff_az);
[out.verdict_el, out.dphi_el] = judge(out.eff_el);

out.msg = sprintf(['[符号自检] 投影效率 方位=%.3f(%s, |Δφ|≈%s)  俯仰=%.3f(%s, |Δφ|≈%s)', ...
                   ' | 目标格 %d%s'], ...
    out.eff_az, out.verdict_az, deg_str(out.dphi_az), ...
    out.eff_el, out.verdict_el, deg_str(out.dphi_el), ...
    n_pairs, hint(out.verdict_az, out.verdict_el));
end

% ---------------------------------------------------------------------
function e = safe_eff(proj_sum, abs_sum)
% 加权平均 |cos Δφ|（权重 |c|）。|c| 累加为 0（无目标格）时给 NaN。
if abs_sum <= 0
    e = NaN;
else
    e = min(1, double(proj_sum) / double(abs_sum));
end
end

function [v, dphi] = judge(e)
if isnan(e)
    v = 'na';  dphi = NaN;  return;
end
dphi = acosd(e);            % 由效率反推 |Δφ|
if     e >= 0.90, v = 'ok';
elseif e >= 0.55, v = 'warn';
else,             v = 'fail';
end
end

function s = deg_str(d)
if isnan(d)
    s = '--';
else
    s = sprintf('%.0f°', d);
end
end

function s = hint(va, ve)
% 只在需要人注意时附一句处置建议；fail 与 warn 的动作不同，所以分开写。
% 注意本自检是"φ 固定成常量"这一决策的唯一兜底（见 apps/run_batch_pipeline.m 中
% cfg.angle.phi_az 处说明）：固定值出错时不会自己报警，只有这里能发现。
if strcmp(va, 'fail') || strcmp(ve, 'fail')
    s = '  ← 符号已被固定偏置主导，本批不可用：立即重标 cfg.angle.phi_az/phi_el（口径见 docs/monopulse_sign_failure.md §7.6）';
elseif strcmp(va, 'warn') || strcmp(ve, 'warn')
    s = '  ← 相位偏向翻号窗口边缘，量程已损失：建议用已知几何的航线重标一次（§7.6）';
else
    s = '';
end
end
