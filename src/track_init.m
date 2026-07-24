function [tracks, track_id_counter] = track_init()
%TRACK_INIT 初始化航迹管理器。
%
%   输出：
%   tracks           : [0×1 struct] - 字段完备的空航迹结构体数组。
%   track_id_counter : [integer]    - 全局航迹 ID 计数器，初始值 1。

track_template = struct( ...
    'track_id',          [], ...    % 航迹唯一 ID
    'state',             [], ...    % 状态向量 [x;vx;y;vy;z;vz] (6×1)
    'covariance',        [], ...    % 状态协方差 P (6×6)
    'timestamps',        [], ...    % 更新时间戳序列
    'last_update',       -inf, ...  % 最后一次量测更新时间
    'consecutive_misses', 0, ...    % 连续丢失次数
    'success_count',     0, ...     % 成功关联次数 (M/N 逻辑)
    'total_count',       0, ...     % 存在总帧数 (M/N 逻辑)
    'prediction_count',  0, ...     % 连续预测次数
    'is_terminated',     false, ... % 是否已终止
    'terminationReason', "", ...    % 终止原因: 'missed' | 'M/N'
    'is_confirmed',      false, ... % 是否已通过 M/N 确认
    'confirmed_at',      [], ...    % 确认时的帧号 (total_count)
    'path',              [], ...    % 历史位置 (x,y,z) [L×3]
    'velocity_history',  [], ...    % 历史速度 (vx,vy,vz) [L×3]
    'last_innov',        [], ...    % 上次更新的新息 (4×1)
    'last_K',            [], ...    % 上次更新的卡尔曼增益 (6×4)
    'last_S',            []);       % 上次更新的新息协方差 (4×4)

tracks = repmat(track_template, 0, 1);
track_id_counter = 1;
end
