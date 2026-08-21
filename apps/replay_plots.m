function replay_plots()
%REPLAY_PLOTS 从保存的 PlotData_*.mat 重新生成三类图（二维点迹/二维航迹/三维航迹）。
%
% 用法：直接运行 replay_plots，在弹出的文件对话框中手动选择要读取的 PlotData_*.mat。
% 输出与批处理管线（run_batch_pipeline.m）完全一致的三类图，
% 保存到所选数据文件同目录下的 Replay_Results 子目录。
%
% 依赖：src/ 下的三个绘图函数文件 plot_point_trace_2d.m /
%       plot_track_map_2d.m / plot_tracking_3d_gif.m。

% 1. 环境：把 src 目录加入路径（复用与管线相同的绘图函数）
this_dir = fileparts(mfilename('fullpath'));
project_root = fileparts(this_dir);
addpath(project_root);
addpath(genpath(fullfile(project_root, 'src')));

% 2. 手动选择要读取的数据文件
[file, path] = uigetfile('*.mat', '选择绘图数据文件 (PlotData_*.mat)');
if isequal(file, 0)
    fprintf('[replay] 已取消。\n');
    return;
end
data_file = fullfile(path, file);
fprintf('[replay] 读取：%s\n', data_file);

% 3. 加载并校验必需字段
S = load(data_file, '-mat');
required = {'fused_plots', 'beam_schedule', 'tracks', 'track_results', ...
    'total_scan_frames', 'radar_height', 'frame_step', 'gif_delay'};
missing = required(~ismember(required, fieldnames(S)));
if ~isempty(missing)
    error('[replay] 数据文件缺少字段: %s', strjoin(missing, ', '));
end

% 4. 输出目录：数据文件同目录下的 Replay_Results
result_dir = fullfile(path, 'Replay_Results');
if ~exist(result_dir, 'dir')
    mkdir(result_dir);
end

% 5. 与批处理管线一致地重画三类图
plot_point_trace_2d(S.fused_plots, S.beam_schedule, result_dir);
plot_track_map_2d(S.tracks, S.beam_schedule, result_dir);
plot_tracking_3d_gif(S.fused_plots, S.total_scan_frames, S.track_results, ...
    S.radar_height, result_dir, S.frame_step, S.gif_delay);

fprintf('[replay] 完成，输出目录：%s\n', result_dir);
end
