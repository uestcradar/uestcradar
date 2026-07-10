function [det_mask, thresh_map] = cfar_2d(pwr_map, params)
%CFAR_2D 二维 CA-CFAR 检测器。
%
% 输入：
%   pwr_map - 待检测的功率图
%   params  - CFAR 参数结构体，必须包含 guard_r、guard_d、ref_r、ref_d 和 pfa
% 输出：
%   det_mask   - 检测掩膜
%   thresh_map  - CFAR 门限图
% 作用：
%   对 RD 功率图做二维 CA-CFAR 检测。
%   单独保留这个模块的原因是：CFAR 门限生成属于独立的检测策略，
%   后续如果要替换成 OS-CFAR、GO-CFAR 或其他检测器，只需要替换这里，
%   不必改动主流程中的聚类、测角和绘图代码。

guard_r = params.guard_r;
guard_d = params.guard_d;
ref_r = params.ref_r;
ref_d = params.ref_d;
pfa = params.pfa;

wr = 2 * (guard_r + ref_r) + 1;
wd = 2 * (guard_d + ref_d) + 1;
win = ones(wr, wd);
win(ref_r + 1:end - ref_r, ref_d + 1:end - ref_d) = 0;
n_ref = sum(win(:));

alpha = n_ref * (pfa^(-1 / n_ref) - 1);
ref_mean = conv2(double(pwr_map), win / n_ref, 'same');

thresh_map = alpha * ref_mean;
det_mask = pwr_map > thresh_map;

br = guard_r + ref_r;
bd = guard_d + ref_d;
det_mask(1:br, :) = false;
det_mask(end - br + 1:end, :) = false;
det_mask(:, 1:bd) = false;
det_mask(:, end - bd + 1:end) = false;
end
