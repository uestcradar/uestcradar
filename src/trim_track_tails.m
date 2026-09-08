function tracks = trim_track_tails(tracks)
%TRIM_TRACK_TAILS 删除每条航迹在最后一次真实量测之后的所有纯预测“拖尾”点。
%
% 输入/输出：
%   tracks : 航迹结构体数组（原地裁剪 path/meas_path/velocity_history/updated_mask/timestamps）。
%
% 作用：
%   航迹丢失量测期间，状态由 CV 模型外推，path 中会写入无测量支撑的纯预测点。
%   若这些点出现在最后一次真实量测之后，会在航迹图上表现为虚假的“拖尾”。
%   本函数把每条航迹裁剪到最后一个 updated_mask==true（真实量测更新）处。

if isempty(tracks)
    return;
end

for i = 1:numel(tracks)
    if ~isfield(tracks, 'updated_mask') || isempty(tracks(i).updated_mask)
        continue;
    end
    last_meas_idx = find(tracks(i).updated_mask, 1, 'last');
    if isempty(last_meas_idx)
        continue;
    end
    tracks(i).path(last_meas_idx + 1:end, :) = [];
    tracks(i).meas_path(last_meas_idx + 1:end, :) = [];
    tracks(i).velocity_history(last_meas_idx + 1:end, :) = [];
    tracks(i).updated_mask(last_meas_idx + 1:end) = [];
    tracks(i).timestamps(last_meas_idx + 1:end) = [];
end
end
