function record_info = scan_bin_record_info(binPath, varargin)
%SCAN_BIN_RECORD_INFO Scan a continuous CYHD XDMA BIN capture.
%   RECORD_INFO = SCAN_BIN_RECORD_INFO(BINPATH) validates the physical frame
%   layout and performs a permissive continuity check on beam metadata.  The
%   IQ payload is never retained, so memory use depends on issue count rather
%   than capture size.
%
%   Name-value options:
%     ProgressFcn  @(fraction, message), fraction is in [0, 1]
%     LogFcn       @(message)
%     CancelFcn    @() logical scalar

%   All byte offsets stored in RECORD_INFO are zero-based file offsets.

%   Frame layout used here (new protocol only):
%     0x00000  128 bytes  frame header (0x5A)
%     0x06DE0   32 bytes  beam metadata (zero-based Word #879)
%     0x06E00  128 bytes  data header (0x606060FE, little-endian)
%     0x06E80  4096 points IQ payload
%     0x26E80  512 bytes  CRC/status region (not validated here)
%     0x27080  128 bytes  frame tail (0xA5)

    parser = inputParser;
    parser.FunctionName = mfilename;
    addRequired(parser, 'binPath', @(x) (ischar(x) || (isstring(x) && isscalar(x))));
    addParameter(parser, 'ProgressFcn', [], @(x) isempty(x) || isa(x, 'function_handle'));
    addParameter(parser, 'LogFcn', [], @(x) isempty(x) || isa(x, 'function_handle'));
    addParameter(parser, 'CancelFcn', [], @(x) isempty(x) || isa(x, 'function_handle'));
    parse(parser, binPath, varargin{:});

    binPath = char(parser.Results.binPath);
    progressFcn = parser.Results.ProgressFcn;
    logFcn = parser.Results.LogFcn;
    cancelFcn = parser.Results.CancelFcn;

    if ~isfile(binPath)
        error('CYHD:FileNotFound', 'BIN 文件不存在: %s', binPath);
    end

    [binDir, binStem, binExt] = fileparts(binPath);
    if isempty(binDir)
        binDir = pwd;
    end
    binPath = fullfile(binDir, [binStem, binExt]);
    fileEntry = dir(binPath);
    if isempty(fileEntry) || fileEntry(1).isdir
        error('CYHD:InvalidInput', '输入路径不是普通文件: %s', binPath);
    end
    fileEntry = fileEntry(1);
    fileSize = double(fileEntry.bytes);

    protocol = protocolConstants();
    framePattern = repmat(uint8(protocol.frame_header_byte), protocol.frame_header_bytes, 1);

    fid = fopen(binPath, 'rb', 'ieee-le');
    if fid == -1
        error('CYHD:OpenFailed', '无法打开 BIN 文件: %s', binPath);
    end
    fileCleanup = onCleanup(@() fclose(fid));

    scanTimer = tic;
    lastProgress = -1;
    lastProgressUpdate = tic;

    issues = emptyIssueColumns();
    segments = emptySegmentColumns();
    gaps = emptyGapColumns();

    candidateCount = uint64(0);
    framedFrameCount = uint64(0);
    validFrameCount = uint64(0);
    headerErrorCount = uint64(0);
    beamMagicErrorCount = uint64(0);
    dataHeaderErrorCount = uint64(0);
    tailErrorCount = uint64(0);
    reservedErrorCount = uint64(0);
    resyncCount = uint64(0);
    skippedBytes = uint64(0);
    leadingBytes = uint64(0);
    trailingBytes = uint64(0);
    truncatedTailBytes = uint64(0);

    bigEndianMetadataCount = uint64(0);
    littleEndianMetadataCount = uint64(0);
    mixedEndianReported = false;
    firstMetadataByteOrder = '';

    sweepHistogram = zeros(32, 1, 'uint64');
    flagsHistogram = zeros(64, 1, 'uint64');
    pulseHistogram = zeros(4096, 1, 'uint64');
    beamHistogram = zeros(512, 1, 'uint64');
    beamPulseMin = inf(512, 1);
    beamPulseMax = -inf(512, 1);
    beamAzMin = inf(512, 1);
    beamAzMax = -inf(512, 1);
    beamElMin = inf(512, 1);
    beamElMax = -inf(512, 1);

    sweepTransitions = emptyTransitionStats();
    beamTransitions = emptyTransitionStats();
    timestampBackwardCount = uint64(0);
    semanticFrameCount = uint64(0);
    firstTimestamp = uint64(0);
    lastTimestamp = uint64(0);
    minTimestamp = uint64(0);
    maxTimestamp = uint64(0);
    haveTimestamp = false;

    havePreviousSemantic = false;
    previousSemanticOffset = -1;
    previousMeta = emptyMetadata();

    segmentActive = false;
    segmentStartOffset = 0;
    segmentEndOffset = 0;
    segmentStartFrame = uint64(0);
    segmentEndFrame = uint64(0);
    segmentFrameCount = uint64(0);
    previousValidOffset = -1;
    haveLastValidFrame = false;
    lastValidFrameIndex = uint64(0);
    lastValidOffset = 0;
    lastValidMeta = emptyMetadata();

    searchOffset = 0;
    foundAnyCandidate = false;

    emitLog(sprintf('开始扫描: %s', binPath));
    publishProgress(0, '正在搜索首帧...', true);

    while searchOffset < fileSize
        checkCancelled();
        % Fast path: after a framed candidate, the next frame should begin
        % exactly 160000 bytes later.  Probe that expected offset directly;
        % only fall back to chunked linear search when synchronization was
        % actually lost.  This avoids rereading an 8 MiB search block for
        % every normal frame.
        if hasFrameHeaderAt(fid, searchOffset, fileSize, framePattern)
            candidateOffset = searchOffset;
        else
            candidateOffset = findFrameHeader(fid, searchOffset, fileSize, framePattern, @searchHeartbeat);
        end

        if candidateOffset < 0
            remaining = fileSize - searchOffset;
            if remaining > 0
                isTruncatedFrame = false;
                if foundAnyCandidate && remaining < protocol.frame_bytes
                    if fseek(fid, searchOffset, 'bof') ~= 0
                        error('CYHD:SeekFailed', '无法定位到尾部偏移 %d。', searchOffset);
                    end
                    tailFragment = fread(fid, remaining, '*uint8');
                    markerPart = min(numel(tailFragment), protocol.frame_header_bytes);
                    isTruncatedFrame = markerPart > 0 && ...
                        all(tailFragment(1:markerPart) == protocol.frame_header_byte);
                end

                if isTruncatedFrame
                    truncatedTailBytes = uint64(remaining);
                    addIssue('BOUNDARY', 'WARNING', 'TRUNCATED_FRAME', candidateCount + uint64(1), ...
                        uint64(searchOffset), uint64(remaining), '', '', '', ...
                        sprintf('文件末尾只有%d字节，不足完整的%d字节帧。', remaining, protocol.frame_bytes));
                else
                    skippedBytes = skippedBytes + uint64(remaining);
                end

                if ~foundAnyCandidate
                    leadingBytes = leadingBytes + uint64(remaining);
                    addIssue('STRUCTURAL', 'ERROR', 'NO_FRAME_FOUND', uint64(0), ...
                        uint64(searchOffset), uint64(remaining), '', '', '', ...
                        '剩余文件中未找到完整的128字节0x5A帧头。');
                elseif ~isTruncatedFrame
                    trailingBytes = trailingBytes + uint64(remaining);
                    addIssue('BOUNDARY', 'WARNING', 'TRAILING_UNFRAMED_BYTES', candidateCount, ...
                        uint64(searchOffset), uint64(remaining), '', '', '', ...
                        '最后一个帧之后存在无法同步的尾部字节。');
                end
            end
            break;
        end

        if candidateOffset > searchOffset
            skipped = candidateOffset - searchOffset;
            skippedBytes = skippedBytes + uint64(skipped);
            if ~foundAnyCandidate
                leadingBytes = leadingBytes + uint64(skipped);
                issueType = 'LEADING_UNFRAMED_BYTES';
                detail = '首个有效帧头之前存在未成帧字节。';
                issueCategory = 'BOUNDARY';
                issueSeverity = 'WARNING';
            else
                resyncCount = resyncCount + uint64(1);
                issueType = 'RESYNC_SKIPPED_BYTES';
                detail = '重新同步到下一帧头前跳过了未成帧字节。';
                issueCategory = 'STRUCTURAL';
                issueSeverity = 'ERROR';
            end
            addIssue(issueCategory, issueSeverity, issueType, candidateCount + uint64(1), ...
                uint64(searchOffset), uint64(skipped), '', '', '', detail);
        end

        foundAnyCandidate = true;
        candidateCount = candidateCount + uint64(1);
        publishProgress(candidateOffset, sprintf('正在检查候选帧 %d...', candidateCount), false);

        if fseek(fid, candidateOffset, 'bof') ~= 0
            error('CYHD:SeekFailed', '无法定位到文件偏移 %d。', candidateOffset);
        end
        frame = fread(fid, protocol.frame_bytes, '*uint8');
        if numel(frame) < protocol.frame_bytes
            available = numel(frame);
            truncatedTailBytes = uint64(available);
            addIssue('BOUNDARY', 'WARNING', 'TRUNCATED_FRAME', candidateCount, ...
                uint64(candidateOffset), uint64(available), '', '', '', ...
                sprintf('文件末尾只有%d字节，不足完整的%d字节帧。', available, protocol.frame_bytes));
            break;
        end

        headerOk = all(frame(1:protocol.frame_header_bytes) == protocol.frame_header_byte);
        if ~headerOk
            headerErrorCount = headerErrorCount + uint64(1);
            addIssue('STRUCTURAL', 'ERROR', 'INVALID_FRAME_HEADER', candidateCount, ...
                uint64(candidateOffset), uint64(protocol.frame_header_bytes), '', '', '', ...
                '帧头不是连续128字节0x5A。');
        end

        beamBytes = frame(protocol.beam_offset + (1:protocol.beam_bytes));
        [meta, beamMagicOk, byteOrder] = decodeBeamMetadata(beamBytes);
        reservedOk = false;
        if beamMagicOk
            if strcmp(byteOrder, 'big-endian')
                bigEndianMetadataCount = bigEndianMetadataCount + uint64(1);
            else
                littleEndianMetadataCount = littleEndianMetadataCount + uint64(1);
            end
            if isempty(firstMetadataByteOrder)
                firstMetadataByteOrder = byteOrder;
            elseif ~strcmp(firstMetadataByteOrder, byteOrder) && ~mixedEndianReported
                mixedEndianReported = true;
                addIssue('SEMANTIC', 'WARNING', 'MIXED_METADATA_BYTE_ORDER', candidateCount, ...
                    uint64(candidateOffset + protocol.beam_offset), uint64(protocol.beam_bytes), ...
                    'metadata_byte_order', firstMetadataByteOrder, byteOrder, ...
                    '同一文件中检测到两种波位元数据字节序。');
            end
            reservedOk = meta.reserved_lo == 0 && meta.reserved_hi == 0;
            if ~reservedOk
                reservedErrorCount = reservedErrorCount + uint64(1);
                addIssue('STRUCTURAL', 'ERROR', 'NONZERO_BEAM_RESERVED', candidateCount, ...
                    uint64(candidateOffset + protocol.beam_offset + 24), uint64(8), ...
                    'reserved', '0', sprintf('0x%08X%08X', meta.reserved_hi, meta.reserved_lo), ...
                    '波位元数据保留64位不是全零。');
            end
        else
            beamMagicErrorCount = beamMagicErrorCount + uint64(1);
            addIssue('STRUCTURAL', 'ERROR', 'INVALID_BEAM_MAGIC', candidateCount, ...
                uint64(candidateOffset + protocol.beam_offset), uint64(4), ...
                'magic', 'BEAM', bytesToHex(beamBytes(1:4)), ...
                '波位区域未检测到BEAM magic。');
        end

        actualDataHeader = frame(protocol.data_header_offset + (1:protocol.data_header_bytes));
        dataHeaderOk = isequal(actualDataHeader, protocol.data_header_pattern);
        if ~dataHeaderOk
            dataHeaderErrorCount = dataHeaderErrorCount + uint64(1);
            addIssue('STRUCTURAL', 'ERROR', 'INVALID_DATA_HEADER', candidateCount, ...
                uint64(candidateOffset + protocol.data_header_offset), uint64(protocol.data_header_bytes), ...
                '', '', '', '96字节数据头不符合{60 60 60 FE}重复格式。');
        end

        tail = frame(protocol.frame_tail_offset + (1:protocol.frame_tail_bytes));
        tailOk = all(tail == protocol.frame_tail_byte);
        if tailOk
            framedFrameCount = framedFrameCount + uint64(1);
        else
            tailErrorCount = tailErrorCount + uint64(1);
            addIssue('STRUCTURAL', 'ERROR', 'INVALID_FRAME_TAIL', candidateCount, ...
                uint64(candidateOffset + protocol.frame_tail_offset), uint64(protocol.frame_tail_bytes), ...
                '', '', '', '预期位置不是连续128字节0xA5帧尾。');
        end

        % The 512-byte region at 0x26E80 carries CRC/status information in
        % real captures.  It is intentionally excluded from frame validity.
        fullyValid = headerOk && beamMagicOk && reservedOk && dataHeaderOk && tailOk;
        if fullyValid
            validFrameCount = validFrameCount + uint64(1);
            semanticFrameCount = semanticFrameCount + uint64(1);

            sweepHistogram(double(meta.sweep_count) + 1) = sweepHistogram(double(meta.sweep_count) + 1) + uint64(1);
            flagsHistogram(double(meta.meta_flags) + 1) = flagsHistogram(double(meta.meta_flags) + 1) + uint64(1);
            pulseHistogram(double(meta.pulse_in_beam) + 1) = pulseHistogram(double(meta.pulse_in_beam) + 1) + uint64(1);
            beamHistogram(double(meta.current_beam_idx) + 1) = beamHistogram(double(meta.current_beam_idx) + 1) + uint64(1);

            beamPos = double(meta.current_beam_idx) + 1;
            beamPulseMin(beamPos) = min(beamPulseMin(beamPos), double(meta.pulse_in_beam));
            beamPulseMax(beamPos) = max(beamPulseMax(beamPos), double(meta.pulse_in_beam));
            beamAzMin(beamPos) = min(beamAzMin(beamPos), double(meta.az_code));
            beamAzMax(beamPos) = max(beamAzMax(beamPos), double(meta.az_code));
            beamElMin(beamPos) = min(beamElMin(beamPos), double(meta.el_code));
            beamElMax(beamPos) = max(beamElMax(beamPos), double(meta.el_code));

            if ~haveTimestamp
                firstTimestamp = meta.timestamp;
                minTimestamp = meta.timestamp;
                maxTimestamp = meta.timestamp;
                haveTimestamp = true;
            else
                minTimestamp = min(minTimestamp, meta.timestamp);
                maxTimestamp = max(maxTimestamp, meta.timestamp);
            end
            lastTimestamp = meta.timestamp;

            isAdjacent = havePreviousSemantic && ...
                candidateOffset == previousSemanticOffset + protocol.frame_bytes;
            if isAdjacent
                [sweepTransitions, sweepIssue] = updateTransition(sweepTransitions, ...
                    double(previousMeta.sweep_count), double(meta.sweep_count));
                if ~isempty(sweepIssue)
                    addSemanticTransitionIssue(sweepIssue, 'sweep_count', ...
                        previousMeta.sweep_count, meta.sweep_count, candidateOffset);
                end

                [beamTransitions, beamIssue] = updateTransition(beamTransitions, ...
                    double(previousMeta.current_beam_idx), double(meta.current_beam_idx));
                if ~isempty(beamIssue)
                    addSemanticTransitionIssue(beamIssue, 'current_beam_idx', ...
                        previousMeta.current_beam_idx, meta.current_beam_idx, candidateOffset);
                end

                if meta.timestamp < previousMeta.timestamp
                    timestampBackwardCount = timestampBackwardCount + uint64(1);
                    addIssue('SEMANTIC', 'WARNING', 'TIMESTAMP_BACKWARD', candidateCount, ...
                        uint64(candidateOffset + protocol.beam_offset + 8), uint64(8), ...
                        'timestamp', sprintf('%u', previousMeta.timestamp), sprintf('%u', meta.timestamp), ...
                        '全局时间戳发生回退。');
                end
            end

            havePreviousSemantic = true;
            previousSemanticOffset = candidateOffset;
            previousMeta = meta;

            physicalAdjacent = segmentActive && ...
                candidateOffset == previousValidOffset + protocol.frame_bytes;
            % Continuous segments are defined only by structurally valid,
            % physically adjacent frames. Forward timestamp jumps are normal
            % during beam scanning and neither warn nor split the stream.
            startsNewSegment = ~segmentActive || ~physicalAdjacent;
            if startsNewSegment
                if haveLastValidFrame
                    if segmentActive
                        previousSegmentId = uint32(numel(segments.frame_count) + 1);
                    else
                        previousSegmentId = uint32(numel(segments.frame_count));
                    end
                    addGap(previousSegmentId, previousSegmentId + uint32(1), ...
                        lastValidFrameIndex, candidateCount, lastValidOffset, ...
                        candidateOffset, lastValidMeta.timestamp, meta.timestamp);
                end
                if segmentActive
                    closeSegment();
                end
                segmentActive = true;
                segmentStartOffset = candidateOffset;
                segmentEndOffset = candidateOffset;
                segmentStartFrame = candidateCount;
                segmentEndFrame = candidateCount;
                segmentFrameCount = uint64(1);
            else
                segmentEndOffset = candidateOffset;
                segmentEndFrame = candidateCount;
                segmentFrameCount = segmentFrameCount + uint64(1);
            end
            previousValidOffset = candidateOffset;
            haveLastValidFrame = true;
            lastValidFrameIndex = candidateCount;
            lastValidOffset = candidateOffset;
            lastValidMeta = meta;
        else
            havePreviousSemantic = false;
            previousSemanticOffset = -1;
            if segmentActive
                closeSegment();
            end
            previousValidOffset = -1;
        end

        if tailOk
            searchOffset = candidateOffset + protocol.frame_bytes;
        else
            searchOffset = candidateOffset + 1;
        end
    end

    if segmentActive
        closeSegment();
    end

    if fileSize == 0
        addIssue('STRUCTURAL', 'ERROR', 'EMPTY_FILE', uint64(0), uint64(0), uint64(0), ...
            '', '', '', 'BIN 文件为空。');
    end

    publishProgress(fileSize, '扫描完成，正在整理报告...', true);

    issueTable = makeIssueTable(issues);
    segmentTable = makeSegmentTable(segments);
    gapTable = makeGapTable(gaps);
    beamSummary = makeBeamSummary(beamHistogram, beamPulseMin, beamPulseMax, ...
        beamAzMin, beamAzMax, beamElMin, beamElMax);

    reliableGapMask = gapTable.inference_reliable;
    inferredMissingFrameCount = sum(gapTable.inferred_missing_frame_count(reliableGapMask), 'native');
    inferredMissingDurationSeconds = sum(gapTable.inferred_missing_duration_seconds(reliableGapMask));
    unresolvedGapCount = uint64(sum(~reliableGapMask));

    structuralIssueCount = uint64(sum(strcmp(issues.category, 'STRUCTURAL')));
    boundaryIssueCount = uint64(sum(strcmp(issues.category, 'BOUNDARY')));
    semanticIssueCount = uint64(sum(strcmp(issues.category, 'SEMANTIC')));
    badFrameCount = candidateCount - validFrameCount;

    if structuralIssueCount > 0 || validFrameCount == 0
        overallStatus = 'FAIL';
    elseif semanticIssueCount > 0 || boundaryIssueCount > 0
        overallStatus = 'WARN';
    else
        overallStatus = 'PASS';
    end

    if bigEndianMetadataCount > 0 && littleEndianMetadataCount == 0
        metadataByteOrder = 'big-endian';
    elseif littleEndianMetadataCount > 0 && bigEndianMetadataCount == 0
        metadataByteOrder = 'little-endian';
    elseif bigEndianMetadataCount > 0 && littleEndianMetadataCount > 0
        metadataByteOrder = 'mixed';
    else
        metadataByteOrder = 'unknown';
    end

    record_info = struct();
    record_info.file = struct( ...
        'name', [binStem, binExt], ...
        'full_path', binPath, ...
        'size_bytes', uint64(fileSize), ...
        'modified_time', fileEntry.date, ...
        'scanned_time', char(datetime('now', 'Format', 'yyyy-MM-dd HH:mm:ss')), ...
        'scan_duration_seconds', toc(scanTimer));
    record_info.protocol = protocol;
    record_info.summary = struct( ...
        'status', overallStatus, ...
        'frame_candidates', candidateCount, ...
        'framed_frame_count', framedFrameCount, ...
        'valid_frame_count', validFrameCount, ...
        'bad_frame_count', badFrameCount, ...
        'structural_issue_count', structuralIssueCount, ...
        'boundary_issue_count', boundaryIssueCount, ...
        'semantic_issue_count', semanticIssueCount, ...
        'continuous_segment_count', uint64(height(segmentTable)), ...
        'gap_count', uint64(height(gapTable)), ...
        'inferred_missing_frame_count', inferredMissingFrameCount, ...
        'inferred_missing_duration_seconds', inferredMissingDurationSeconds, ...
        'unresolved_gap_count', unresolvedGapCount);
    record_info.structural = struct( ...
        'header_error_count', headerErrorCount, ...
        'beam_magic_error_count', beamMagicErrorCount, ...
        'data_header_error_count', dataHeaderErrorCount, ...
        'tail_error_count', tailErrorCount, ...
        'beam_reserved_error_count', reservedErrorCount, ...
        'crc_region_checked', false, ...
        'resync_count', resyncCount, ...
        'skipped_bytes', skippedBytes, ...
        'leading_bytes', leadingBytes, ...
        'trailing_bytes', trailingBytes, ...
        'truncated_tail_bytes', truncatedTailBytes, ...
        'metadata_byte_order', metadataByteOrder, ...
        'big_endian_metadata_count', bigEndianMetadataCount, ...
        'little_endian_metadata_count', littleEndianMetadataCount);
    record_info.semantic = struct( ...
        'checked_frame_count', semanticFrameCount, ...
        'sweep_count', sweepTransitions, ...
        'current_beam_idx', beamTransitions, ...
        'timestamp_backward_count', timestampBackwardCount, ...
        'has_timestamp', haveTimestamp, ...
        'first_timestamp', firstTimestamp, ...
        'last_timestamp', lastTimestamp, ...
        'min_timestamp', minTimestamp, ...
        'max_timestamp', maxTimestamp, ...
        'sweep_count_histogram', sweepHistogram, ...
        'meta_flags_histogram', flagsHistogram, ...
        'pulse_in_beam_histogram', pulseHistogram, ...
        'current_beam_idx_histogram', beamHistogram);
    record_info.beam_summary = beamSummary;
    record_info.segments = segmentTable;
    record_info.gaps = gapTable;
    record_info.issues = issueTable;

    emitLog(sprintf(['扫描完成: %s，状态=%s，有效帧=%d，坏帧=%d，', ...
        '不连续点=%d，推断缺失帧=%d，推断缺失时长=%.6f秒。'], ...
        binPath, overallStatus, validFrameCount, badFrameCount, height(gapTable), ...
        inferredMissingFrameCount, inferredMissingDurationSeconds));
    publishProgress(fileSize, '扫描报告已生成。', true);

    function publishProgress(byteOffset, message, force)
        if fileSize <= 0
            fraction = 1;
        else
            fraction = min(max(double(byteOffset) / fileSize, 0), 1);
        end
        if force || fraction - lastProgress >= 0.005 || toc(lastProgressUpdate) >= 0.25
            checkCancelled();
            if ~isempty(progressFcn)
                progressFcn(fraction, message);
            end
            lastProgress = fraction;
            lastProgressUpdate = tic;
        end
    end

    function searchHeartbeat(byteOffset)
        publishProgress(byteOffset, '正在重新搜索帧头...', false);
    end

    function checkCancelled()
        if ~isempty(cancelFcn) && cancelFcn()
            error('CYHD:ScanCancelled', '用户取消了 BIN 扫描。');
        end
    end

    function emitLog(message)
        if ~isempty(logFcn)
            logFcn(message);
        end
    end

    function addIssue(category, severity, type, frameIndex, byteOffset, lengthBytes, ...
            field, previousValue, currentValue, detail)
        row = numel(issues.type) + 1;
        issues.category{row, 1} = category;
        issues.severity{row, 1} = severity;
        issues.type{row, 1} = type;
        issues.frame_index(row, 1) = uint64(frameIndex);
        issues.byte_offset(row, 1) = uint64(byteOffset);
        issues.length_bytes(row, 1) = uint64(lengthBytes);
        issues.field{row, 1} = field;
        issues.previous_value{row, 1} = previousValue;
        issues.current_value{row, 1} = currentValue;
        issues.detail{row, 1} = detail;
    end

    function addSemanticTransitionIssue(issueKind, field, previousValue, currentValue, candidate)
        if strcmp(issueKind, 'jump_forward')
            type = 'COUNTER_JUMP_FORWARD';
            detail = '字段向前跨级，疑似存在缺失数据。';
        else
            type = 'COUNTER_BACKWARD_NONZERO';
            detail = '字段发生非零回退，不符合保持、加1或回零规则。';
        end
        addIssue('SEMANTIC', 'WARNING', type, candidateCount, uint64(candidate + protocol.beam_offset), ...
            uint64(protocol.beam_bytes), field, sprintf('%u', previousValue), ...
            sprintf('%u', currentValue), detail);
    end

    function closeSegment()
        row = numel(segments.frame_count) + 1;
        segments.segment_id(row, 1) = uint32(row);
        segments.start_frame_index(row, 1) = segmentStartFrame;
        segments.end_frame_index(row, 1) = segmentEndFrame;
        segments.start_byte_offset(row, 1) = uint64(segmentStartOffset);
        segments.end_byte_offset(row, 1) = uint64(segmentEndOffset + protocol.frame_bytes - 1);
        segments.frame_count(row, 1) = segmentFrameCount;
        segmentActive = false;
        segmentFrameCount = uint64(0);
    end

    function addGap(previousSegmentId, nextSegmentId, previousFrameIndex, nextFrameIndex, ...
            previousOffset, nextOffset, previousTimestamp, nextTimestamp)
        row = numel(gaps.gap_id) + 1;
        gaps.gap_id(row, 1) = uint32(row);
        gaps.previous_segment_id(row, 1) = previousSegmentId;
        gaps.next_segment_id(row, 1) = nextSegmentId;
        gaps.previous_frame_index(row, 1) = previousFrameIndex;
        gaps.next_frame_index(row, 1) = nextFrameIndex;
        gaps.previous_frame_offset(row, 1) = uint64(previousOffset);
        gaps.next_frame_offset(row, 1) = uint64(nextOffset);
        previousFrameEnd = previousOffset + protocol.frame_bytes;
        gaps.physical_gap_bytes(row, 1) = uint64(max(nextOffset - previousFrameEnd, 0));
        gaps.previous_timestamp(row, 1) = previousTimestamp;
        gaps.next_timestamp(row, 1) = nextTimestamp;

        reliable = false;
        deltaTicks = uint64(0);
        missingFrames = uint64(0);
        missingTicks = uint64(0);
        missingSeconds = NaN;
        expectedStep = protocol.timestamp_ticks_per_frame;

        if nextTimestamp <= previousTimestamp
            note = '时间戳未向前递增，无法推断缺失时长。';
        else
            deltaTicks = nextTimestamp - previousTimestamp;
            if deltaTicks < expectedStep
                note = '时间戳增量小于正常单帧增量，无法推断缺失时长。';
            elseif mod(deltaTicks, expectedStep) ~= 0
                note = '时间戳增量不是正常单帧增量的整数倍，无法可靠推断。';
            else
                reliable = true;
                missingFrames = deltaTicks / expectedStep - uint64(1);
                missingTicks = deltaTicks - expectedStep;
                missingSeconds = double(missingTicks) / protocol.timestamp_tick_hz;
                note = '按相邻正常帧时间戳增加4096推断。';
            end
        end

        gaps.timestamp_delta_ticks(row, 1) = deltaTicks;
        gaps.inference_reliable(row, 1) = reliable;
        gaps.inferred_missing_frame_count(row, 1) = missingFrames;
        gaps.inferred_missing_timestamp_ticks(row, 1) = missingTicks;
        gaps.inferred_missing_duration_seconds(row, 1) = missingSeconds;
        gaps.note{row, 1} = note;
    end
end

function protocol = protocolConstants()
    protocol = struct();
    protocol.version = 'CYHD continuous frame, BEAM at Word 879 (new protocol only)';
    protocol.frame_bytes = 160000;
    protocol.frame_header_byte = uint8(hex2dec('5A'));
    protocol.frame_header_bytes = 128;
    protocol.beam_offset = hex2dec('06DE0');
    protocol.beam_bytes = 32;
    protocol.beam_magic = uint32(hex2dec('4245414D'));
    protocol.data_header_offset = hex2dec('06E00');
    protocol.data_header_bytes = 128;
    % 0x606060FE is stored as a little-endian uint32 on the recorded wire.
    protocol.data_header_pattern = repmat(uint8([hex2dec('FE'); hex2dec('60'); hex2dec('60'); hex2dec('60')]), 32, 1);
    protocol.iq_payload_offset = hex2dec('06E80');
    protocol.iq_payload_points = 4096;
    protocol.iq_payload_bytes = 131072;
    protocol.crc_region_offset = hex2dec('26E80');
    protocol.crc_region_bytes = 512;
    protocol.crc_region_checked = false;
    protocol.timestamp_ticks_per_frame = uint64(4096);
    protocol.timestamp_tick_hz = 30.72e6;
    protocol.frame_tail_offset = hex2dec('27080');
    protocol.frame_tail_bytes = 128;
    protocol.frame_tail_byte = uint8(hex2dec('A5'));
end

function matched = hasFrameHeaderAt(fid, offset, fileSize, pattern)
    matched = false;
    if offset < 0 || offset + numel(pattern) > fileSize
        return;
    end
    if fseek(fid, offset, 'bof') ~= 0
        error('CYHD:SeekFailed', '无法定位到预期帧偏移 %d。', offset);
    end
    bytes = fread(fid, numel(pattern), '*uint8');
    matched = isequal(bytes, pattern);
end

function candidateOffset = findFrameHeader(fid, startOffset, fileSize, pattern, heartbeatFcn)
    searchChunkBytes = 8 * 1024 * 1024;
    overlapBytes = numel(pattern) - 1;
    carry = zeros(0, 1, 'uint8');
    readOffset = startOffset;
    candidateOffset = -1;

    if fseek(fid, startOffset, 'bof') ~= 0
        error('CYHD:SeekFailed', '无法定位到搜索起点 %d。', startOffset);
    end

    while readOffset < fileSize
        amount = min(searchChunkBytes, fileSize - readOffset);
        raw = fread(fid, amount, '*uint8');
        if isempty(raw)
            return;
        end

        combined = [carry; raw];
        combinedOffset = readOffset - numel(carry);
        hits = strfind(combined.', pattern.');
        if ~isempty(hits)
            positions = combinedOffset + hits - 1;
            positions = positions(positions >= startOffset);
            if ~isempty(positions)
                candidateOffset = positions(1);
                return;
            end
        end

        readOffset = readOffset + numel(raw);
        keep = min(overlapBytes, numel(combined));
        carry = combined(end - keep + 1:end);
        heartbeatFcn(readOffset);
    end
end

function [meta, magicOk, byteOrder] = decodeBeamMetadata(bytes)
    meta = emptyMetadata();
    byteOrder = '';
    asciiMagic = uint8([hex2dec('42'); hex2dec('45'); hex2dec('41'); hex2dec('4D')]);
    reversedMagic = flipud(asciiMagic);

    if isequal(bytes(1:4), asciiMagic)
        byteOrder = 'big-endian';
    elseif isequal(bytes(1:4), reversedMagic)
        byteOrder = 'little-endian';
    else
        magicOk = false;
        return;
    end
    magicOk = true;

    words = zeros(8, 1, 'uint32');
    for wordIndex = 1:8
        part = bytes((wordIndex - 1) * 4 + (1:4));
        words(wordIndex) = bytesToUint32(part, byteOrder);
    end

    scanMeta0 = words(2);
    meta.magic = words(1);
    meta.scan_meta0 = scanMeta0;
    meta.sweep_count = uint8(bitand(bitshift(scanMeta0, -27), uint32(31)));
    meta.meta_flags = uint8(bitand(bitshift(scanMeta0, -21), uint32(63)));
    meta.pulse_in_beam = uint16(bitand(bitshift(scanMeta0, -9), uint32(4095)));
    meta.current_beam_idx = uint16(bitand(scanMeta0, uint32(511)));
    meta.timestamp_lo = words(3);
    meta.timestamp_hi = words(4);
    meta.timestamp = bitor(bitshift(uint64(words(4)), 32), uint64(words(3)));
    meta.az_code = uint16(bitand(words(5), uint32(65535)));
    meta.el_code = uint16(bitand(words(6), uint32(65535)));
    meta.reserved_lo = words(7);
    meta.reserved_hi = words(8);
end

function value = bytesToUint32(bytes, byteOrder)
    b = uint32(bytes);
    if strcmp(byteOrder, 'big-endian')
        value = bitor(bitor(bitshift(b(1), 24), bitshift(b(2), 16)), ...
            bitor(bitshift(b(3), 8), b(4)));
    else
        value = bitor(bitor(b(1), bitshift(b(2), 8)), ...
            bitor(bitshift(b(3), 16), bitshift(b(4), 24)));
    end
end

function meta = emptyMetadata()
    meta = struct( ...
        'magic', uint32(0), ...
        'scan_meta0', uint32(0), ...
        'sweep_count', uint8(0), ...
        'meta_flags', uint8(0), ...
        'pulse_in_beam', uint16(0), ...
        'current_beam_idx', uint16(0), ...
        'timestamp_lo', uint32(0), ...
        'timestamp_hi', uint32(0), ...
        'timestamp', uint64(0), ...
        'az_code', uint16(0), ...
        'el_code', uint16(0), ...
        'reserved_lo', uint32(0), ...
        'reserved_hi', uint32(0));
end

function stats = emptyTransitionStats()
    stats = struct( ...
        'hold_count', uint64(0), ...
        'increment_count', uint64(0), ...
        'wrap_to_zero_count', uint64(0), ...
        'jump_forward_count', uint64(0), ...
        'backward_nonzero_count', uint64(0));
end

function [stats, issueKind] = updateTransition(stats, previousValue, currentValue)
    issueKind = '';
    if currentValue == previousValue
        stats.hold_count = stats.hold_count + uint64(1);
    elseif currentValue == previousValue + 1
        stats.increment_count = stats.increment_count + uint64(1);
    elseif currentValue == 0
        stats.wrap_to_zero_count = stats.wrap_to_zero_count + uint64(1);
    elseif currentValue > previousValue + 1
        stats.jump_forward_count = stats.jump_forward_count + uint64(1);
        issueKind = 'jump_forward';
    else
        stats.backward_nonzero_count = stats.backward_nonzero_count + uint64(1);
        issueKind = 'backward_nonzero';
    end
end

function issues = emptyIssueColumns()
    issues = struct( ...
        'category', {cell(0, 1)}, ...
        'severity', {cell(0, 1)}, ...
        'type', {cell(0, 1)}, ...
        'frame_index', zeros(0, 1, 'uint64'), ...
        'byte_offset', zeros(0, 1, 'uint64'), ...
        'length_bytes', zeros(0, 1, 'uint64'), ...
        'field', {cell(0, 1)}, ...
        'previous_value', {cell(0, 1)}, ...
        'current_value', {cell(0, 1)}, ...
        'detail', {cell(0, 1)});
end

function issueTable = makeIssueTable(issues)
    issueTable = table(issues.category, issues.severity, issues.type, ...
        issues.frame_index, issues.byte_offset, issues.length_bytes, ...
        issues.field, issues.previous_value, issues.current_value, issues.detail, ...
        'VariableNames', {'category', 'severity', 'type', 'frame_index', ...
        'byte_offset', 'length_bytes', 'field', 'previous_value', ...
        'current_value', 'detail'});
end

function segments = emptySegmentColumns()
    segments = struct( ...
        'segment_id', zeros(0, 1, 'uint32'), ...
        'start_frame_index', zeros(0, 1, 'uint64'), ...
        'end_frame_index', zeros(0, 1, 'uint64'), ...
        'start_byte_offset', zeros(0, 1, 'uint64'), ...
        'end_byte_offset', zeros(0, 1, 'uint64'), ...
        'frame_count', zeros(0, 1, 'uint64'));
end

function segmentTable = makeSegmentTable(segments)
    segmentTable = table(segments.segment_id, segments.start_frame_index, ...
        segments.end_frame_index, segments.start_byte_offset, ...
        segments.end_byte_offset, segments.frame_count, ...
        'VariableNames', {'segment_id', 'start_frame_index', 'end_frame_index', ...
        'start_byte_offset', 'end_byte_offset', 'frame_count'});
end

function gaps = emptyGapColumns()
    gaps = struct( ...
        'gap_id', zeros(0, 1, 'uint32'), ...
        'previous_segment_id', zeros(0, 1, 'uint32'), ...
        'next_segment_id', zeros(0, 1, 'uint32'), ...
        'previous_frame_index', zeros(0, 1, 'uint64'), ...
        'next_frame_index', zeros(0, 1, 'uint64'), ...
        'previous_frame_offset', zeros(0, 1, 'uint64'), ...
        'next_frame_offset', zeros(0, 1, 'uint64'), ...
        'physical_gap_bytes', zeros(0, 1, 'uint64'), ...
        'previous_timestamp', zeros(0, 1, 'uint64'), ...
        'next_timestamp', zeros(0, 1, 'uint64'), ...
        'timestamp_delta_ticks', zeros(0, 1, 'uint64'), ...
        'inference_reliable', false(0, 1), ...
        'inferred_missing_frame_count', zeros(0, 1, 'uint64'), ...
        'inferred_missing_timestamp_ticks', zeros(0, 1, 'uint64'), ...
        'inferred_missing_duration_seconds', zeros(0, 1), ...
        'note', {cell(0, 1)});
end

function gapTable = makeGapTable(gaps)
    gapTable = table(gaps.gap_id, gaps.previous_segment_id, gaps.next_segment_id, ...
        gaps.previous_frame_index, gaps.next_frame_index, ...
        gaps.previous_frame_offset, gaps.next_frame_offset, gaps.physical_gap_bytes, ...
        gaps.previous_timestamp, gaps.next_timestamp, gaps.timestamp_delta_ticks, ...
        gaps.inference_reliable, gaps.inferred_missing_frame_count, ...
        gaps.inferred_missing_timestamp_ticks, gaps.inferred_missing_duration_seconds, ...
        gaps.note, 'VariableNames', {'gap_id', 'previous_segment_id', ...
        'next_segment_id', 'previous_frame_index', 'next_frame_index', ...
        'previous_frame_offset', 'next_frame_offset', 'physical_gap_bytes', ...
        'previous_timestamp', 'next_timestamp', 'timestamp_delta_ticks', ...
        'inference_reliable', 'inferred_missing_frame_count', ...
        'inferred_missing_timestamp_ticks', 'inferred_missing_duration_seconds', 'note'});
end

function beamSummary = makeBeamSummary(beamHistogram, pulseMin, pulseMax, azMin, azMax, elMin, elMax)
    positions = find(beamHistogram > 0);
    beamId = uint16(positions - 1);
    frameCount = beamHistogram(positions);
    pulseMinValue = uint16(pulseMin(positions));
    pulseMaxValue = uint16(pulseMax(positions));
    azMinValue = uint16(azMin(positions));
    azMaxValue = uint16(azMax(positions));
    elMinValue = uint16(elMin(positions));
    elMaxValue = uint16(elMax(positions));
    beamSummary = table(beamId, frameCount, pulseMinValue, pulseMaxValue, ...
        azMinValue, azMaxValue, elMinValue, elMaxValue, ...
        'VariableNames', {'beam_id', 'frame_count', 'pulse_min', 'pulse_max', ...
        'az_code_min', 'az_code_max', 'el_code_min', 'el_code_max'});
end

function text = bytesToHex(bytes)
    text = upper(strjoin(cellstr(dec2hex(bytes, 2)), ' '));
end
