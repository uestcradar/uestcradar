function save_summary = save_bin_data_mat(binPath, record_info, priSamples, varargin)
%SAVE_BIN_DATA_MAT Convert valid CYHD frame segments to algorithm-ready MAT files.
%   Each output MAT contains one variable named DATA.  Channel matrices are
%   complex single arrays shaped [slow-time PRI x fast-time sample].

    parser = inputParser;
    parser.FunctionName = mfilename;
    addRequired(parser, 'binPath', @(x) ischar(x) || (isstring(x) && isscalar(x)));
    addRequired(parser, 'record_info', @isstruct);
    addRequired(parser, 'priSamples', @(x) isnumeric(x) && isscalar(x) && ...
        isfinite(x) && x >= 1 && x == floor(x));
    addParameter(parser, 'OutputDir', '', @(x) ischar(x) || (isstring(x) && isscalar(x)));
    addParameter(parser, 'Overwrite', false, @(x) islogical(x) && isscalar(x));
    addParameter(parser, 'ProgressFcn', [], @(x) isempty(x) || isa(x, 'function_handle'));
    addParameter(parser, 'LogFcn', [], @(x) isempty(x) || isa(x, 'function_handle'));
    parse(parser, binPath, record_info, priSamples, varargin{:});

    binPath = char(parser.Results.binPath);
    priSamples = double(priSamples);
    outputDir = char(parser.Results.OutputDir);
    overwrite = parser.Results.Overwrite;
    progressFcn = parser.Results.ProgressFcn;
    logFcn = parser.Results.LogFcn;

    if ~isfile(binPath)
        error('CYHD:FileNotFound', 'BIN 文件不存在: %s', binPath);
    end
    if isempty(outputDir)
        outputDir = fileparts(binPath);
        if isempty(outputDir)
            outputDir = pwd;
        end
    end
    if ~isfolder(outputDir)
        error('CYHD:OutputDirNotFound', '输出目录不存在: %s', outputDir);
    end
    validateRecordInfo(record_info);

    fileEntry = dir(binPath);
    fileEntry = fileEntry(1);
    if uint64(fileEntry.bytes) ~= record_info.file.size_bytes || ...
            ~strcmp(fileEntry.date, record_info.file.modified_time)
        error('CYHD:SourceChanged', 'BIN 文件在校验后发生变化，请重新校验。');
    end

    protocol = record_info.protocol;
    frameBytes = double(protocol.frame_bytes);
    payloadOffset = double(protocol.iq_payload_offset);
    payloadPoints = double(protocol.iq_payload_points);
    payloadBytes = double(protocol.iq_payload_bytes);
    segments = record_info.segments;
    [~, binStem] = fileparts(binPath);

    segmentCount = height(segments);
    targetPaths = cell(segmentCount, 1);
    segmentPriCounts = zeros(segmentCount, 1, 'uint64');
    segmentKeptSamples = zeros(segmentCount, 1, 'uint64');
    segmentDiscardedSamples = zeros(segmentCount, 1, 'uint64');
    willSave = false(segmentCount, 1);

    for row = 1:segmentCount
        frameCount = segments.frame_count(row);
        totalSamples = frameCount * uint64(payloadPoints);
        priCount = idivide(totalSamples, uint64(priSamples), 'floor');
        keptSamples = priCount * uint64(priSamples);
        segmentPriCounts(row) = priCount;
        segmentKeptSamples(row) = keptSamples;
        segmentDiscardedSamples(row) = totalSamples - keptSamples;
        willSave(row) = priCount > 0;
        targetPaths{row} = fullfile(outputDir, sprintf('%s_data_seg%03d.mat', ...
            binStem, segments.segment_id(row)));
    end

    existingTargets = targetPaths(willSave & cellfun(@isfile, targetPaths));
    if ~overwrite && ~isempty(existingTargets)
        error('CYHD:OutputExists', '目标文件已存在: %s', existingTargets{1});
    end

    fid = fopen(binPath, 'rb', 'ieee-le');
    if fid == -1
        error('CYHD:OpenFailed', '无法打开 BIN 文件: %s', binPath);
    end
    fileCleanup = onCleanup(@() fclose(fid));

    totalFramesToRead = sum(segments.frame_count(willSave), 'native');
    completedFrames = uint64(0);
    savedPaths = cell(segmentCount, 1);
    savedSegmentIds = zeros(segmentCount, 1, 'uint32');
    savedFrameCounts = zeros(segmentCount, 1, 'uint64');
    savedPriCounts = zeros(segmentCount, 1, 'uint64');
    savedSampleCounts = zeros(segmentCount, 1, 'uint64');
    savedDiscardedCounts = zeros(segmentCount, 1, 'uint64');
    savedCount = 0;

    emitLog(sprintf('开始保存数据: %s', binPath));
    for segmentRow = 1:segmentCount
        segmentId = segments.segment_id(segmentRow);
        frameCountU64 = segments.frame_count(segmentRow);
        priCountU64 = segmentPriCounts(segmentRow);
        discardedSamples = segmentDiscardedSamples(segmentRow);

        if ~willSave(segmentRow)
            emitLog(sprintf('连续段%d不足一个完整PRI，跳过%s帧、%s采样点。', ...
                segmentId, formatUint(frameCountU64), ...
                formatUint(frameCountU64 * uint64(payloadPoints))));
            continue;
        end

        frameCount = double(frameCountU64);
        priCount = double(priCountU64);
        emitLog(sprintf('正在解析连续段%d: %s帧，%s个完整PRI。', ...
            segmentId, formatUint(frameCountU64), formatUint(priCountU64)));

        ch0 = complex(zeros(priCount, priSamples, 'single'));
        ch1 = complex(zeros(priCount, priSamples, 'single'));
        ch2 = complex(zeros(priCount, priSamples, 'single'));
        frameTimestamp = zeros(frameCount, 1, 'uint64');
        frameSweep = zeros(frameCount, 1, 'uint8');
        framePulse = zeros(frameCount, 1, 'uint16');
        frameBeam = zeros(frameCount, 1, 'uint16');
        frameAz = zeros(frameCount, 1, 'uint16');
        frameEl = zeros(frameCount, 1, 'uint16');

        segmentStart = double(segments.start_byte_offset(segmentRow));
        if fseek(fid, segmentStart, 'bof') ~= 0
            error('CYHD:SeekFailed', '无法定位连续段%d，偏移=%s。', ...
                segmentId, formatUint(segments.start_byte_offset(segmentRow)));
        end

        destinationPri = 1;
        destinationFastTime = 1;
        keptRemaining = segmentKeptSamples(segmentRow);
        chunkFrames = 64;
        frameBase = 0;

        while frameBase < frameCount
            framesThisChunk = min(chunkFrames, frameCount - frameBase);
            raw = fread(fid, [frameBytes, framesThisChunk], '*uint8');
            if size(raw, 1) ~= frameBytes || size(raw, 2) ~= framesThisChunk
                error('CYHD:UnexpectedEOF', '读取连续段%d时遇到文件尾。', segmentId);
            end

            for localFrame = 1:framesThisChunk
                frameIndex = frameBase + localFrame;
                frame = raw(:, localFrame);
                meta = validateAndDecodeFrame(frame, protocol, frameIndex, segmentId);
                frameTimestamp(frameIndex) = meta.timestamp;
                frameSweep(frameIndex) = meta.sweep_count;
                framePulse(frameIndex) = meta.pulse_in_beam;
                frameBeam(frameIndex) = meta.current_beam_idx;
                frameAz(frameIndex) = meta.az_code;
                frameEl(frameIndex) = meta.el_code;

                takeCount = double(min(keptRemaining, uint64(payloadPoints)));
                if takeCount > 0
                    payload = frame(payloadOffset + (1:payloadBytes));
                    iq = decodePayload(payload, payloadPoints);
                    sourceStart = 1;
                    while sourceStart <= takeCount
                        availableInPri = priSamples - destinationFastTime + 1;
                        amount = min(availableInPri, takeCount - sourceStart + 1);
                        sourceRange = sourceStart:(sourceStart + amount - 1);
                        destinationRange = destinationFastTime:(destinationFastTime + amount - 1);
                        ch0(destinationPri, destinationRange) = complex( ...
                            single(iq(1, sourceRange)), single(iq(2, sourceRange)));
                        ch1(destinationPri, destinationRange) = complex( ...
                            single(iq(3, sourceRange)), single(iq(4, sourceRange)));
                        ch2(destinationPri, destinationRange) = complex( ...
                            single(iq(5, sourceRange)), single(iq(6, sourceRange)));
                        sourceStart = sourceStart + amount;
                        destinationFastTime = destinationFastTime + amount;
                        if destinationFastTime > priSamples
                            destinationPri = destinationPri + 1;
                            destinationFastTime = 1;
                        end
                    end
                    keptRemaining = keptRemaining - uint64(takeCount);
                end
            end

            frameBase = frameBase + framesThisChunk;
            completedFrames = completedFrames + uint64(framesThisChunk);
            publishProgress(completedFrames, totalFramesToRead, ...
                sprintf('正在解析连续段%d...', segmentId));
        end

        % Preserve one metadata record per original XDMA frame. Do not
        % expand, repeat, or interpolate frame metadata onto the PRI axis.
        beam = struct();
        beam.timestamp = frameTimestamp;
        beam.sweep_count = frameSweep;
        beam.pulse_in_beam = framePulse;
        beam.current_beam_idx = frameBeam;
        beam.az_code = frameAz;
        beam.el_code = frameEl;

        data = struct();
        data.sample_rate = double(protocol.timestamp_tick_hz);
        data.pri_samples = uint32(priSamples);
        data.ch0 = ch0;
        data.ch1 = ch1;
        data.ch2 = ch2;
        data.beam = beam;

        targetPath = targetPaths{segmentRow};
        emitLog(sprintf('正在写入MAT: %s', targetPath));
        atomicSaveData(targetPath, data);

        savedCount = savedCount + 1;
        outputRow = savedCount;
        savedPaths{outputRow, 1} = targetPath;
        savedSegmentIds(outputRow, 1) = segmentId;
        savedFrameCounts(outputRow, 1) = frameCountU64;
        savedPriCounts(outputRow, 1) = priCountU64;
        savedSampleCounts(outputRow, 1) = segmentKeptSamples(segmentRow);
        savedDiscardedCounts(outputRow, 1) = discardedSamples;
        emitLog(sprintf('已保存连续段%d: %s帧，%s个PRI，丢弃尾部%s点。', ...
            segmentId, formatUint(frameCountU64), formatUint(priCountU64), ...
            formatUint(discardedSamples)));

        clear data beam ch0 ch1 ch2 frameTimestamp frameSweep framePulse frameBeam frameAz frameEl raw;
    end

    savedPaths = savedPaths(1:savedCount);
    savedSegmentIds = savedSegmentIds(1:savedCount);
    savedFrameCounts = savedFrameCounts(1:savedCount);
    savedPriCounts = savedPriCounts(1:savedCount);
    savedSampleCounts = savedSampleCounts(1:savedCount);
    savedDiscardedCounts = savedDiscardedCounts(1:savedCount);

    save_summary = struct();
    save_summary.output_dir = outputDir;
    save_summary.data_file_count = uint64(numel(savedPaths));
    save_summary.saved_frame_count = sum(savedFrameCounts, 'native');
    save_summary.saved_pri_count = sum(savedPriCounts, 'native');
    save_summary.saved_sample_count = sum(savedSampleCounts, 'native');
    save_summary.discarded_sample_count = sum(savedDiscardedCounts, 'native');
    save_summary.skipped_bad_frame_count = record_info.summary.bad_frame_count;
    save_summary.files = table(savedSegmentIds, savedFrameCounts, savedPriCounts, ...
        savedSampleCounts, savedDiscardedCounts, savedPaths, ...
        'VariableNames', {'segment_id', 'frame_count', 'pri_count', ...
        'sample_count', 'discarded_sample_count', 'path'});

    publishProgress(totalFramesToRead, totalFramesToRead, '保存完成。');
    emitLog(sprintf('数据保存完成: %s个MAT，%s帧，%s个PRI。', ...
        formatUint(save_summary.data_file_count), ...
        formatUint(save_summary.saved_frame_count), ...
        formatUint(save_summary.saved_pri_count)));

    function publishProgress(doneFrames, allFrames, message)
        if isempty(progressFcn)
            return;
        end
        if allFrames == 0
            fraction = 1;
        else
            fraction = double(doneFrames) / double(allFrames);
        end
        progressFcn(min(max(fraction, 0), 1), message);
    end

    function emitLog(message)
        if ~isempty(logFcn)
            logFcn(message);
        end
    end
end

function validateRecordInfo(info)
    requiredFields = {'file', 'protocol', 'summary', 'segments'};
    if ~all(isfield(info, requiredFields))
        error('CYHD:InvalidRecordInfo', 'record_info 缺少保存所需字段。');
    end
    if ~istable(info.segments)
        error('CYHD:InvalidRecordInfo', 'record_info.segments 必须是表。');
    end

    protocolFields = {'frame_bytes', 'beam_offset', 'beam_bytes', 'beam_magic', ...
        'data_header_offset', 'data_header_bytes', 'data_header_pattern', ...
        'iq_payload_offset', 'iq_payload_points', 'iq_payload_bytes', ...
        'frame_tail_offset', 'frame_tail_bytes'};
    if ~all(isfield(info.protocol, protocolFields))
        error('CYHD:InvalidRecordInfo', 'record_info.protocol 缺少保存所需字段。');
    end

    expectedHeader = repmat(uint8([hex2dec('FE'); hex2dec('60'); ...
        hex2dec('60'); hex2dec('60')]), 32, 1);
    isNewProtocol = double(info.protocol.frame_bytes) == 160000 && ...
        double(info.protocol.beam_offset) == hex2dec('06DE0') && ...
        double(info.protocol.beam_bytes) == 32 && ...
        uint32(info.protocol.beam_magic) == uint32(hex2dec('4245414D')) && ...
        double(info.protocol.data_header_offset) == hex2dec('06E00') && ...
        double(info.protocol.data_header_bytes) == 128 && ...
        isequal(info.protocol.data_header_pattern(:), expectedHeader) && ...
        double(info.protocol.iq_payload_offset) == hex2dec('06E80') && ...
        double(info.protocol.iq_payload_points) == 4096 && ...
        double(info.protocol.iq_payload_bytes) == 131072 && ...
        double(info.protocol.frame_tail_offset) == hex2dec('27080') && ...
        double(info.protocol.frame_tail_bytes) == 128;
    if ~isNewProtocol
        error('CYHD:UnsupportedProtocol', ...
            ['仅支持新协议：BEAM=Word #879/0x06DE0，' ...
             'DataHeader=0x06E00/128字节。请重新校验BIN文件。']);
    end
end

function iq = decodePayload(payload, payloadPoints)
    lowBytes = uint16(payload(1:2:end));
    highBytes = bitshift(uint16(payload(2:2:end)), 8);
    unsignedWords = bitor(lowBytes, highBytes);
    signedWords = typecast(unsignedWords, 'int16');
    iq = reshape(signedWords, 16, payloadPoints);
end

function meta = validateAndDecodeFrame(frame, protocol, frameIndex, segmentId)
    if ~all(frame(1:double(protocol.frame_header_bytes)) == protocol.frame_header_byte)
        invalidFrame('帧头');
    end

    beamBytes = frame(double(protocol.beam_offset) + (1:double(protocol.beam_bytes)));
    words = littleEndianUint32(beamBytes);
    if words(1) ~= protocol.beam_magic
        invalidFrame('BEAM magic');
    end
    if words(7) ~= 0 || words(8) ~= 0
        invalidFrame('BEAM reserved');
    end

    actualDataHeader = frame(double(protocol.data_header_offset) + ...
        (1:double(protocol.data_header_bytes)));
    if ~isequal(actualDataHeader, protocol.data_header_pattern)
        invalidFrame('数据头');
    end

    tail = frame(double(protocol.frame_tail_offset) + (1:double(protocol.frame_tail_bytes)));
    if ~all(tail == protocol.frame_tail_byte)
        invalidFrame('帧尾');
    end

    scanMeta0 = words(2);
    meta = struct();
    meta.sweep_count = uint8(bitand(bitshift(scanMeta0, -27), uint32(31)));
    meta.pulse_in_beam = uint16(bitand(bitshift(scanMeta0, -9), uint32(4095)));
    meta.current_beam_idx = uint16(bitand(scanMeta0, uint32(511)));
    meta.timestamp = bitor(uint64(words(3)), bitshift(uint64(words(4)), 32));
    meta.az_code = uint16(bitand(words(5), uint32(65535)));
    meta.el_code = uint16(bitand(words(6), uint32(65535)));

    function invalidFrame(fieldName)
        error('CYHD:SourceChanged', ...
            '连续段%d的第%d帧%s校验失败；BIN可能已变化，请重新校验。', ...
            segmentId, frameIndex, fieldName);
    end
end

function words = littleEndianUint32(bytes)
    bytes = reshape(uint32(bytes), 4, []);
    words = bytes(1, :) + bitshift(bytes(2, :), 8) + ...
        bitshift(bytes(3, :), 16) + bitshift(bytes(4, :), 24);
    words = words(:);
end

function atomicSaveData(outputPath, data)
    outputDir = fileparts(outputPath);
    temporaryPath = [tempname(outputDir), '.mat'];
    cleanup = onCleanup(@() deleteIfExists(temporaryPath));
    save(temporaryPath, 'data', '-v7.3');
    [ok, message] = movefile(temporaryPath, outputPath, 'f');
    if ~ok
        error('CYHD:SaveFailed', '无法保存数据文件: %s', message);
    end
end

function deleteIfExists(path)
    if isfile(path)
        delete(path);
    end
end

function text = formatUint(value)
    text = sprintf('%u', uint64(value));
end
