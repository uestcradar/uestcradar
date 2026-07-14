function beam_schedule = parse_beam_schedule(beam_file_path)
%PARSE_BEAM_SCHEDULE 解析波位排布文件。
%
% 输入：
%   beam_file_path - 波位格式文件路径（.txt）
% 输出：
%   beam_schedule  - 结构体，字段：
%       .beam_positions   : N×2 double, [az_deg, el_deg]，每行一个波位
%       .pulses_per_dwell : N×1 double, 各波位驻留脉冲数
%       .num_beams        : 波位总数
%       .az_grid          : 唯一位方位角（度）
%       .el_grid          : 唯一位俯仰角（度）
%       .total_pulses     : 一轮扫描总脉冲数
% 作用：
%   按照"波位格式.txt"的规则读入波位排布，跳过注释与表头行，
%   为后续按波位拆分数据提供统一入口。

if ~exist(beam_file_path, 'file')
    error('parse_beam_schedule:FileNotFound', '波位文件不存在：%s', beam_file_path);
end

fid = fopen(beam_file_path, 'r');
if fid == -1
    error('parse_beam_schedule:OpenFailed', '无法打开波位文件：%s', beam_file_path);
end
cleanup = onCleanup(@() fclose(fid));

beam_az = [];
beam_el = [];
beam_pulses = [];

line_idx = 0;
while ~feof(fid)
    raw_line = strtrim(fgetl(fid));
    line_idx = line_idx + 1;

    % 跳过空行和注释行
    if isempty(raw_line) || strncmp(raw_line, '#', 1)
        continue;
    end

    % 替换制表符为空格，统一分隔符
    raw_line = strrep(raw_line, sprintf('\t'), ' ');

    % 按逗号或空格分割
    parts = regexp(raw_line, '[,\s]+', 'split');

    % 过滤空字符串
    parts = parts(~cellfun('isempty', parts));
    if isempty(parts)
        continue;
    end

    % 检查是否是非数字表头行
    test_num = str2double(parts{1});
    if isnan(test_num)
        continue;  % 表头行，跳过
    end

    % 解析数值列
    az_val = str2double(parts{1});
    el_val = str2double(parts{2});
    if numel(parts) >= 3
        p_val = str2double(parts{3});
        if isnan(p_val)
            p_val = 256;  % 默认值
        end
    else
        p_val = 256;
    end

    if isnan(az_val) || isnan(el_val)
        continue;
    end

    beam_az(end + 1) = az_val;   %#ok<AGROW>
    beam_el(end + 1) = el_val;   %#ok<AGROW>
    beam_pulses(end + 1) = p_val; %#ok<AGROW>
end

if isempty(beam_az)
    error('parse_beam_schedule:NoBeams', '波位文件中未解析到任何有效波位：%s', beam_file_path);
end

num_beams = numel(beam_az);
beam_schedule = struct();
beam_schedule.beam_positions = [beam_az(:), beam_el(:)];
beam_schedule.pulses_per_dwell = beam_pulses(:);
beam_schedule.num_beams = num_beams;
beam_schedule.az_grid = unique(beam_az);
beam_schedule.el_grid = unique(beam_el);
beam_schedule.total_pulses = sum(beam_pulses);

fprintf('[波位] 已加载 %d 个波位，方位范围 [%.1f°, %.1f°]，俯仰范围 [%.1f°, %.1f°]，总脉冲 %d\n', ...
    num_beams, min(beam_az), max(beam_az), min(beam_el), max(beam_el), beam_schedule.total_pulses);
end
