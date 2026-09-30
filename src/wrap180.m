function y = wrap180(x)
%WRAP180 把角度折算到 [−180, 180)。
%
% 与 MATLAB 的 wrapToPi 语义对应，但**输入输出都是度**。本项目 cfg.angle.* 全程用度
% （phi_az = -113、exp(-1i*deg2rad(phi_az))），若拿 wrapToPi（收弧度）去折算度值，
% 会得到看似合理的错误结果 —— 这是 docs/monopulse_sign_failure.md §7.1 记录的第二类
% 正负号事故。所有涉及 φ 的折算一律走这里。
%
% 注：wrapToPi 的区间是 (−180, 180]，本函数是 [−180, 180)；两者只在恰好 ±180° 上不同，
% 对 φ（实测 ≈ −93.5°）无影响。

y = mod(x + 180, 360) - 180;
end
