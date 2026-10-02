function beam_schedule = build_beam_schedule_from_meta(parse_bundle, pri_per_frame, scan_mode)
%BUILD_BEAM_SCHEDULE_FROM_META 从新格式 parse_bundle 构建 beam_schedule。
%
% 新格式 mat 内含显式 current_beam_idx (0..38)，直接用其确定波位顺序与扫描边界。
% 支持两种扫描模式（由主函数 cfg.scan_mode 开关切换）：
%   'sawtooth' 锯齿波(单向)：一轮扫描 = 固定 39 波位顺序，扫描间 beam_idx 回卷跳变(|diff|>1)；
%              beam 偏移固定 = (beam_id-1)*n_cpi。用于后续实验。
%   'triangle' 三角往返(蛇形)：一轮扫描 = 一个方向(38 驻留，端点波位在相邻方向共享)，
%              波位顺序逐轮交替(降序/升序)；用 sweep_count 定位折返点；每轮方向记录于
%              scan_direction(+1 升序 / -1 降序)，供 RD 层按方向翻转偏移。
% 同时用 frame_timestamp + t0 计算每轮扫描的代表时间（决策3：中间波位中间时间戳）。
% 首轮（采集起始可能截断）丢弃，末尾 floor 自动丢弃残帧。
%
% 输入:  parse_bundle (含 beam_meta / frame_timestamp / rx_param.t0 / rx_param.fs)
%        pri_per_frame (固定 4)
%        scan_mode     ('sawtooth' | 'triangle')
% 输出:  beam_schedule (含 scan_times、beam_idx、scan_direction、scan_mode 供下游)

    if nargin < 3 || isempty(scan_mode)
        scan_mode = 'sawtooth';
    end

    beam_meta = parse_bundle.beam_meta;

    % ---- 帧级数组（元数据按帧记录，每 pri_per_frame 取一帧）----
    frame_az   = double(beam_meta.az_deg(1:pri_per_frame:end));
    frame_el   = double(beam_meta.el_deg(1:pri_per_frame:end));
    frame_idx  = double(beam_meta.current_beam_idx(1:pri_per_frame:end));
    frame_sw   = double(beam_meta.sweep_count(1:pri_per_frame:end));
    n_frames   = numel(frame_idx);

    frame_ts   = double(parse_bundle.frame_timestamp);
    t0         = double(parse_bundle.rx_param.t0);
    fs         = double(parse_bundle.rx_param.sample_rate);

    % ---- 1. 扫描边界 ----
    switch scan_mode
        case 'sawtooth'
            % 扫描边界 = beam_idx 回卷（|跳变|>1）
            scan_start_frames = find(abs(diff(frame_idx)) > 1) + 1;
        case 'triangle'
            % 扫描边界 = sweep_count 跳变（折返点，每方向一次）
            swc = find(diff(frame_sw) ~= 0);
            scan_start_frames = swc + 1;
    end
    if isempty(scan_start_frames)
        error('build_beam_schedule_from_meta:NoScans', ...
            '未检测到完整扫描（scan_mode=%s）。', scan_mode);
    end

    % ---- 2. 波位顺序 ----
    switch scan_mode
        case 'sawtooth'
            % 锯齿波：波位顺序 = 一轮“完整”扫描内的首现时序（即物理扫描顺序）。
            % 不能直接用整个记录的 unique(...,'stable')：采集若从扫描中途开始，首轮被截断，
            % 其出现顺序相对后续完整扫描是轮转过的（例如 7..38,0..6），而 beam_offset 是从
            % 首轮完整扫描起算的，两者错位会让每个 beam_id 都落到错误的驻留上。
            if numel(scan_start_frames) >= 2
                probe_len = scan_start_frames(2) - scan_start_frames(1);
                probe_end = min(scan_start_frames(1) + probe_len - 1, n_frames);
                uniq_idx  = unique(frame_idx(scan_start_frames(1):probe_end), 'stable');
            else
                % 不足两轮扫描，无法判断完整扫描内的顺序，退回全记录首现序
                uniq_idx = unique(frame_idx, 'stable');
            end
        case 'triangle'
            % 三角：波位顺序 = 升序规范序 (0..38 = 波位1..39)，扫描顺序逐轮交替
            uniq_idx = unique(frame_idx);   % 升序
        otherwise
            error('build_beam_schedule_from_meta:BadScanMode', ...
                '未知扫描模式 "%s"（应为 sawtooth 或 triangle）。', scan_mode);
    end
    num_beams = numel(uniq_idx);
    if num_beams < 1
        error('build_beam_schedule_from_meta:NoBeams', '未能从 current_beam_idx 识别波位。');
    end

    beam_az = zeros(num_beams, 1);
    beam_el = zeros(num_beams, 1);
    for b = 1:num_beams
        f = find(frame_idx == uniq_idx(b), 1, 'first');
        beam_az(b) = frame_az(f);
        beam_el(b) = frame_el(f);
    end

    % ---- 3. 每扫描帧数 / 每波位帧数 ----
    if numel(scan_start_frames) >= 2
        frames_per_scan = scan_start_frames(2) - scan_start_frames(1);
    else
        frames_per_scan = n_frames - scan_start_frames(1) + 1;
    end
    switch scan_mode
        case 'sawtooth'
            dwells_per_scan = num_beams;        % 39 波位
        case 'triangle'
            dwells_per_scan = num_beams - 1;    % 38 驻留（端点共享）
    end
    frames_per_beam = frames_per_scan / dwells_per_scan;
    if frames_per_beam ~= round(frames_per_beam)
        error('build_beam_schedule_from_meta:BadFrameCount', ...
            '每扫描帧数 %d 不能被 %d 个驻留整除。', frames_per_scan, dwells_per_scan);
    end

    % ---- 4. 丢弃首轮截断扫描，确定初始 PRI 偏移 ----
    initial_scan_frame = scan_start_frames(1);
    initial_scan_pri   = (initial_scan_frame - 1) * pri_per_frame;
    total_scans = floor((n_frames - initial_scan_frame + 1) / frames_per_scan);

    % ---- 5. 派生值 ----
    pulses_per_dwell = repmat(frames_per_beam * pri_per_frame, 1, num_beams);
    total_pulses     = frames_per_scan * pri_per_frame;

    % ---- 6. 每轮扫描方向（triangle 用；sawtooth 恒 +1 占位）----
    scan_direction = ones(1, total_scans);
    if strcmp(scan_mode, 'triangle')
        for k = 1:total_scans
            sf = initial_scan_frame + (k - 1) * frames_per_scan;
            idx_a = frame_idx(sf);
            idx_b = frame_idx(min(sf + frames_per_beam, n_frames));
            scan_direction(k) = sign(idx_b - idx_a);   % +1 升序(0->38), -1 降序(38->0)
        end
    end

    % ---- 7. 每轮扫描代表时间（决策3：中间波位中间帧）----
    mid_beam_ordinal  = ceil(dwells_per_scan / 2);
    mid_frame_ordinal = ceil(frames_per_beam / 2);
    mid_frame_offset  = (mid_beam_ordinal - 1) * frames_per_beam + (mid_frame_ordinal - 1);
    mid_frames = initial_scan_frame + mid_frame_offset + (0:total_scans-1) * frames_per_scan;
    scan_times = t0 + (frame_ts(mid_frames) - frame_ts(1)) / fs;

    % ---- 组装输出 ----
    beam_schedule = struct();
    beam_schedule.num_beams = num_beams;
    beam_schedule.beam_positions = [beam_az, beam_el];
    beam_schedule.pulses_per_dwell = pulses_per_dwell;
    beam_schedule.total_pulses = total_pulses;
    beam_schedule.total_scans = total_scans;
    beam_schedule.invalid_scans = [];                 % 新格式显式波位索引，无需异常扫描剔除
    beam_schedule.initial_scan_pri = initial_scan_pri;
    beam_schedule.beam_idx = uniq_idx(:)';            % 各波位对应的 current_beam_idx（规范序）
    beam_schedule.scan_times = scan_times;            % 每轮扫描代表时间 (s, Unix)
    beam_schedule.scan_mode = scan_mode;              % 扫描模式（回传供 RD 层使用）
    beam_schedule.scan_direction = scan_direction;    % 每轮扫描方向 (+1 升序 / -1 降序)

    fprintf('[波位] 模式=%s：%d 波位, %d 帧/驻留, %d 驻留/扫描, %d 帧/扫描, %d 完整扫描\n', ...
        scan_mode, num_beams, frames_per_beam, dwells_per_scan, frames_per_scan, total_scans);
    fprintf('[波位] 首完整扫描帧号=%d, 初始 PRI 偏移=%d, 扫描周期=%.4f s\n', ...
        initial_scan_frame, initial_scan_pri, mean(diff(scan_times)));
    if strcmp(scan_mode, 'triangle')
        fprintf('[波位] 扫描方向(前%d): %s\n', min(12, total_scans), ...
            mat2str(scan_direction(1:min(12, total_scans))));
    end
    for b = 1:num_beams
        fprintf('[波位 %3d] az=%+7.2f°, el=%+7.2f°, idx=%2d\n', ...
            b, beam_az(b), beam_el(b), uniq_idx(b));
    end
end
