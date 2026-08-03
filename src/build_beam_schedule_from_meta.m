function beam_schedule = build_beam_schedule_from_meta(beam_meta, pri_per_frame)
%BUILD_BEAM_SCHEDULE_FROM_META 从新格式 beam_meta 构建 beam_schedule。
%
% 用波位1的出现间隔检测扫描边界，取第二轮的波位帧数作为模板。
% 首轮（可能因前导帧数偏差）丢弃，末尾 floor 自动丢弃残帧。
% 新增：逐轮验证波位帧数是否匹配模板，剔除异常扫描轮次。
%
% 输入:  beam_meta, pri_per_frame (固定为4)
% 输出:  beam_schedule (含 invalid_scans 供检测后过滤)

frame_az = beam_meta.az_deg(1:pri_per_frame:end);
frame_el = beam_meta.el_deg(1:pri_per_frame:end);
frame_valid = beam_meta.meta_valid(1:pri_per_frame:end);
frame_sweep = double(beam_meta.sweep_count(1:pri_per_frame:end));
n_frames = numel(frame_az);

n_valid = sum(frame_valid);
fprintf('[波位] 帧级有效元数据：%d/%d (%.1f%%)\n', n_valid, n_frames, n_valid/n_frames*100);
if n_valid > 0 && n_valid < n_frames
    valid_idx = find(frame_valid);
    fprintf('[波位] 有效帧号范围：%d ~ %d，间隔均值=%.1f 帧\n', ...
        valid_idx(1), valid_idx(end), mean(diff(valid_idx)));
    fprintf('[波位] 有效帧中的唯一方位角：%s\n', mat2str(unique(frame_az(frame_valid)), 3));
end

if ~any(frame_valid)
    error('build_beam_schedule_from_meta:NoValidMeta', ...
        'beam_meta 中没有有效的波位元数据。');
end

% ---- 步骤 1: 扫描边界检测（先用任意稳定波位做 sweep_count 跟踪）----
first_valid = find(frame_valid, 1, 'first');
b1_az = frame_az(first_valid); b1_el = frame_el(first_valid);
b1_frames = find(frame_valid & abs(frame_az - b1_az) < 0.001 & abs(frame_el - b1_el) < 0.001);

% 同一扫描内所有波位1帧的 sweep_count 相同，跳变处即为扫描边界
b1_sweep = frame_sweep(b1_frames);
scan_starts = [1; find(diff(b1_sweep) ~= 0) + 1];  % 索引为 b1_frames 下标
total_scans_raw = numel(scan_starts);
fprintf('[波位] sweep_count 扫描检测：%d 个扫描边界\n', total_scans_raw);

% ---- 步骤 2: 用 sweep_count 定位扫描2首帧，提取波位顺序 ----
scan2_sweep = b1_sweep(scan_starts(2));
s2_start = find(frame_sweep == scan2_sweep & frame_valid, 1, 'first');
scan3_sweep = mod(scan2_sweep + 1, 32);
s3_candidates = find(frame_sweep == scan3_sweep & frame_valid);
s3_start = s3_candidates(find(s3_candidates > s2_start, 1, 'first'));
scan2_start = s2_start;
scan2_end   = s3_start - 1;
fprintf('[波位] 扫描 2 帧范围: %d→%d (sweep=%d)\n', scan2_start, scan2_end, scan2_sweep);

uniq_az = []; uniq_el = [];
for fi = scan2_start:scan2_end
    if ~frame_valid(fi), continue; end
    az = frame_az(fi); el = frame_el(fi);
    if isempty(uniq_az) || ~any(abs(uniq_az - az) < 0.001 & abs(uniq_el - el) < 0.001)
        uniq_az(end+1) = az; %#ok<AGROW>
        uniq_el(end+1) = el; %#ok<AGROW>
    end
end
num_beams = numel(uniq_az);
if num_beams < 1
    error('build_beam_schedule_from_meta:NoBeams', '未能识别任何波位。');
end
% 用正确的波位1重新计算 b1_frames 和 scan_starts
b1_az = uniq_az(1); b1_el = uniq_el(1);
b1_frames = find(frame_valid & abs(frame_az - b1_az) < 0.001 & abs(frame_el - b1_el) < 0.001);
b1_sweep = frame_sweep(b1_frames);
scan_starts = [1; find(diff(b1_sweep) ~= 0) + 1];
total_scans_raw = numel(scan_starts);
fprintf('[波位] 用扫描2波位1 (az=%.2f°) 重建边界：%d 扫描\n', b1_az, total_scans_raw);

% ---- 步骤 3: 提取模板（默认用第二轮，若第二轮异常则顺延）----
scan_frame_counts = [];
template_scan_idx = 0;

for trial = 2:min(total_scans_raw, 5)
    s_start = b1_frames(scan_starts(trial));
    counts = zeros(1, num_beams);
    cur_az = b1_az; cur_el = b1_el; bi = 1; cnt = 0;
    s_end   = b1_frames(scan_starts(trial + 1)) - 1;  % 限制在本轮内
	    for fi = s_start:s_end
        if ~frame_valid(fi), continue; end
        if abs(frame_az(fi) - cur_az) < 0.001 && abs(frame_el(fi) - cur_el) < 0.001
            cnt = cnt + 1;
        else
            counts(bi) = cnt;
            bi = bi + 1; if bi > num_beams, break; end
            cur_az = frame_az(fi); cur_el = frame_el(fi);
            cnt = 1;
        end
    end
    if bi <= num_beams, counts(bi) = cnt; end

    % 简单自检：模板必须覆盖全部波位且每波位帧数>0
    if all(counts > 0)
        scan_frame_counts = counts;
        template_scan_idx = trial;
        break;
    end
end

if isempty(scan_frame_counts)
    error('build_beam_schedule_from_meta:NoValidTemplate', ...
        '无法从扫描 %d~%d 中提取有效波位模板。', 2, min(total_scans_raw, 5));
end

fprintf('[波位] 模板来自第 %d 轮扫描：%s\n', template_scan_idx, mat2str(scan_frame_counts));

% ---- 步骤 4: 逐轮验证，剔除异常扫描 ----
% 对每轮扫描统计各波位帧数，与模板比对
invalid_scans = [];
scan_frame_counts_all = zeros(total_scans_raw, num_beams);

for s = 1:total_scans_raw
    scan_start_frame = b1_frames(scan_starts(s));
    if s < total_scans_raw
        scan_end_frame = b1_frames(scan_starts(s+1)) - 1;
    else
        scan_end_frame = n_frames;
    end

    % 逐帧统计该扫描中各波位的帧数
    cur_beam = 1;
    cnt = 0;
    scan_ok = true;
    s_counts = zeros(1, num_beams);

    for fi = scan_start_frame:scan_end_frame
        if ~frame_valid(fi)
            continue;
        end

        az = frame_az(fi);
        el = frame_el(fi);

        % 检查是否仍属于当前波位
        if cur_beam <= num_beams && ...
           abs(az - uniq_az(cur_beam)) < 0.001 && ...
           abs(el - uniq_el(cur_beam)) < 0.001
            cnt = cnt + 1;
        elseif cur_beam < num_beams && ...
               abs(az - uniq_az(cur_beam + 1)) < 0.001 && ...
               abs(el - uniq_el(cur_beam + 1)) < 0.001
            % 正常过渡到下一波位
            s_counts(cur_beam) = cnt;
            cur_beam = cur_beam + 1;
            cnt = 1;
        else
            % 非预期波位 → 异常
            scan_ok = false;
            break;
        end
    end

    if scan_ok && cnt > 0 && cur_beam <= num_beams
        s_counts(cur_beam) = cnt;
    end

    scan_frame_counts_all(s, :) = s_counts;

    % 判定：必须覆盖全部波位且每波位帧数与模板一致
    if ~scan_ok || cur_beam ~= num_beams || any(s_counts ~= scan_frame_counts)
        invalid_scans(end+1) = s; %#ok<AGROW>
    end
end

% ---- 步骤 5: 找到最后一个异常扫描，丢弃它及之前的所有 ----
invalid_scans = sort(invalid_scans);

% 打印异常扫描诊断
if ~isempty(invalid_scans)
    fprintf('[波位] 检测到 %d 个异常扫描: %s\n', ...
        numel(invalid_scans), mat2str(invalid_scans));
    for s = invalid_scans(:)'
        if all(scan_frame_counts_all(s, :) == 0)
            fprintf('[波位]   扫描 #%d: 无有效元数据\n', s);
        else
            fprintf('[波位]   扫描 #%d 帧数/波位: %s (模板=%s)\n', ...
                s, mat2str(scan_frame_counts_all(s, :)), mat2str(scan_frame_counts));
        end
    end
end

% 排除末轮（由 total_scans 自然丢弃），找最后一个内部异常扫描
invalid_before_end = invalid_scans(invalid_scans < total_scans_raw);
if ~isempty(invalid_before_end)
    first_valid_scan = max(invalid_before_end) + 1;
else
    first_valid_scan = 1;
end

% 有效扫描起始 PRI 偏移（0-based，相对数据流起点）
initial_scan_pri = (b1_frames(scan_starts(first_valid_scan)) - 1) * pri_per_frame;

% 仅保留 first_valid_scan 到 total_scans_raw-1（末轮丢弃）
total_scans = total_scans_raw - first_valid_scan;

if total_scans <= 0
    error('build_beam_schedule_from_meta:NoValidScans', ...
        '异常扫描占比过大：%d 异常 / %d 总数，无有效扫描可处理。', ...
        numel(invalid_before_end), total_scans_raw);
end

fprintf('[波位] 丢弃扫描 #1~#%d（含异常），从 #%d 开始处理，共 %d 有效扫描\n', ...
    first_valid_scan - 1, first_valid_scan, total_scans);
fprintf('[波位] 初始 PRI 偏移=%d (帧 #%d 波位1)\n', ...
    initial_scan_pri, b1_frames(scan_starts(first_valid_scan)));

% ---- 步骤 6: 计算派生值 ----
frames_per_scan = sum(scan_frame_counts);
pulses_per_scan = frames_per_scan * pri_per_frame;
pulses_per_dwell = scan_frame_counts * pri_per_frame;

% ---- 构建输出 ----
beam_schedule = struct();
beam_schedule.num_beams = num_beams;
beam_schedule.beam_positions = [uniq_az(:), uniq_el(:)];
beam_schedule.pulses_per_dwell = pulses_per_dwell;
beam_schedule.total_pulses = pulses_per_scan;
beam_schedule.total_scans = total_scans;
beam_schedule.invalid_scans = invalid_scans;             % 异常扫描信息（仅供参考，不参与 RD）
beam_schedule.initial_scan_pri = initial_scan_pri;       % 首个有效扫描的 PRI 偏移（process_rd_beam 用）

fprintf('[波位] 从 beam_meta 提取：%d 波位, %d 帧/扫描, %d/%d 扫描（%d 异常）\n', ...
    num_beams, frames_per_scan, total_scans, total_scans_raw, numel(invalid_scans));
for b = 1:beam_schedule.num_beams
    fprintf('[波位 %3d] az=%+7.2f°, el=%+7.2f°, pulses/scan=%d\n', ...
        b, beam_schedule.beam_positions(b, 1), ...
        beam_schedule.beam_positions(b, 2), ...
        beam_schedule.pulses_per_dwell(b));
end
end
