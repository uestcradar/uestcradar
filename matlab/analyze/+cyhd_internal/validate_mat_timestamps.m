function report = validate_mat_timestamps(matPath, varargin)
%VALIDATE_MAT_TIMESTAMPS Check every frame timestamp in one data MAT.
%   Only /data/beam/timestamp and the sample-rate scalar are read. Channel
%   datasets are never touched. Timestamp entries are read in chunks, but
%   every adjacent transition is checked.

    parser = inputParser;
    parser.FunctionName = mfilename;
    addRequired(parser, 'matPath', @(x) ischar(x) || (isstring(x) && isscalar(x)));
    addParameter(parser, 'ChunkEntries', 1000000, @(x) isnumeric(x) && ...
        isscalar(x) && isfinite(x) && x >= 1 && x == floor(x));
    addParameter(parser, 'MaxStoredIssues', 100, @(x) isnumeric(x) && ...
        isscalar(x) && isfinite(x) && x >= 0 && x == floor(x));
    addParameter(parser, 'ProgressFcn', [], @(x) isempty(x) || isa(x, 'function_handle'));
    parse(parser, matPath, varargin{:});

    matPath = char(parser.Results.matPath);
    chunkEntries = double(parser.Results.ChunkEntries);
    maxStoredIssues = double(parser.Results.MaxStoredIssues);
    progressFcn = parser.Results.ProgressFcn;

    if ~isfile(matPath)
        error('CYHD:FileNotFound', 'MAT 文件不存在: %s', matPath);
    end

    try
        timestampInfo = h5info(matPath, '/data/beam/timestamp');
        sampleRate = double(h5read(matPath, '/data/sample_rate'));
    catch ME
        error('CYHD:InvalidDataMat', ...
            'MAT 文件缺少时间戳校验所需字段: %s', ME.message);
    end

    timestampSize = double(timestampInfo.Dataspace.Size);
    if numel(timestampSize) ~= 2 || all(timestampSize ~= 1)
        error('CYHD:InvalidDataMat', 'data.beam.timestamp 必须是一维向量。');
    end
    timestampCount = prod(timestampSize);
    if timestampCount < 1
        error('CYHD:InvalidDataMat', 'data.beam.timestamp 为空。');
    end
    if ~isscalar(sampleRate) || ~isfinite(sampleRate) || sampleRate <= 0
        error('CYHD:InvalidDataMat', 'data.sample_rate 不是有效采样率。');
    end

    expectedStep = uint64(4096);
    timer = tic;
    discontinuityCount = uint64(0);
    unchangedCount = uint64(0);
    advanced4096Count = uint64(0);
    forwardGapCount = uint64(0);
    backwardCount = uint64(0);
    irregularForwardCount = uint64(0);
    inferredMissingFrameCount = uint64(0);
    forwardMissingTicks = uint64(0);
    firstTimestamp = uint64(0);
    lastTimestamp = uint64(0);
    previousTimestamp = uint64(0);
    havePrevious = false;

    issueIndex = zeros(maxStoredIssues, 1, 'uint64');
    issuePrevious = zeros(maxStoredIssues, 1, 'uint64');
    issueCurrent = zeros(maxStoredIssues, 1, 'uint64');
    issueSignedDelta = zeros(maxStoredIssues, 1, 'int64');
    issueMissingFrame = zeros(maxStoredIssues, 1, 'uint64');
    issueType = cell(maxStoredIssues, 1);
    storedIssueCount = 0;

    position = 1;
    while position <= timestampCount
        amount = min(chunkEntries, timestampCount - position + 1);
        timestamps = readTimestampChunk(matPath, timestampSize, position, amount);
        timestamps = uint64(timestamps(:));

        for localIndex = 1:numel(timestamps)
            currentTimestamp = timestamps(localIndex);
            currentIndex = uint64(position + localIndex - 1);
            if ~havePrevious
                firstTimestamp = currentTimestamp;
                havePrevious = true;
            else
                [isContinuous, relation, signedDelta, missingFrames, missingTicks] = ...
                    checkTransition(previousTimestamp, currentTimestamp, expectedStep);
                if isContinuous
                    if strcmp(relation, 'UNCHANGED')
                        unchangedCount = unchangedCount + uint64(1);
                    else
                        advanced4096Count = advanced4096Count + uint64(1);
                    end
                else
                    discontinuityCount = discontinuityCount + uint64(1);
                    switch relation
                        case 'FORWARD_GAP'
                            forwardGapCount = forwardGapCount + uint64(1);
                            inferredMissingFrameCount = inferredMissingFrameCount + missingFrames;
                            forwardMissingTicks = forwardMissingTicks + missingTicks;
                        case 'FORWARD_IRREGULAR'
                            irregularForwardCount = irregularForwardCount + uint64(1);
                            forwardMissingTicks = forwardMissingTicks + missingTicks;
                        case 'BACKWARD'
                            backwardCount = backwardCount + uint64(1);
                    end

                    if storedIssueCount < maxStoredIssues
                        storedIssueCount = storedIssueCount + 1;
                        issueIndex(storedIssueCount) = currentIndex;
                        issuePrevious(storedIssueCount) = previousTimestamp;
                        issueCurrent(storedIssueCount) = currentTimestamp;
                        issueSignedDelta(storedIssueCount) = signedDelta;
                        issueMissingFrame(storedIssueCount) = missingFrames;
                        issueType{storedIssueCount} = relation;
                    end
                end
            end
            previousTimestamp = currentTimestamp;
            lastTimestamp = currentTimestamp;
        end

        position = position + amount;
        if ~isempty(progressFcn)
            progressFcn(min((position - 1) / timestampCount, 1), ...
                sprintf('正在校验时间戳 %d/%d...', position - 1, timestampCount));
        end
    end

    issueIndex = issueIndex(1:storedIssueCount);
    issuePrevious = issuePrevious(1:storedIssueCount);
    issueCurrent = issueCurrent(1:storedIssueCount);
    issueSignedDelta = issueSignedDelta(1:storedIssueCount);
    issueMissingFrame = issueMissingFrame(1:storedIssueCount);
    issueType = issueType(1:storedIssueCount);

    if discontinuityCount == 0
        status = 'PASS';
    else
        status = 'FAIL';
    end

    report = struct();
    report.file_path = matPath;
    report.status = status;
    report.timestamp_count = uint64(timestampCount);
    report.transition_count = uint64(max(timestampCount - 1, 0));
    report.allowed_step_ticks = uint64([0; expectedStep]);
    report.expected_advance_ticks = expectedStep;
    report.sample_rate = sampleRate;
    report.first_timestamp = firstTimestamp;
    report.last_timestamp = lastTimestamp;
    report.discontinuity_count = discontinuityCount;
    report.unchanged_transition_count = unchangedCount;
    report.advanced_4096_transition_count = advanced4096Count;
    report.forward_gap_count = forwardGapCount;
    report.irregular_forward_count = irregularForwardCount;
    report.backward_count = backwardCount;
    report.inferred_missing_frame_count = inferredMissingFrameCount;
    report.forward_missing_ticks = forwardMissingTicks;
    report.forward_missing_duration_seconds = double(forwardMissingTicks) / sampleRate;
    report.scan_duration_seconds = toc(timer);
    report.stored_issue_count = uint64(storedIssueCount);
    report.issues_truncated = discontinuityCount > uint64(storedIssueCount);
    report.issues = table(issueIndex, issuePrevious, issueCurrent, ...
        issueSignedDelta, issueMissingFrame, issueType, ...
        'VariableNames', {'current_frame_index', 'previous_timestamp', ...
        'current_timestamp', 'signed_delta_ticks', 'inferred_missing_frame', 'type'});
end

function timestamps = readTimestampChunk(matPath, datasetSize, position, amount)
    if datasetSize(1) == 1
        start = [1, position];
        count = [1, amount];
    else
        start = [position, 1];
        count = [amount, 1];
    end
    timestamps = h5read(matPath, '/data/beam/timestamp', start, count);
end

function [continuous, relation, signedDelta, missingFrames, missingTicks] = ...
        checkTransition(previous, current, expectedStep)
    missingFrames = uint64(0);
    missingTicks = uint64(0);
    if current > previous
        delta = current - previous;
        signedDelta = int64(delta);
        if delta == expectedStep
            continuous = true;
            relation = 'ADVANCE_4096';
        elseif delta > expectedStep && mod(delta, expectedStep) == 0
            continuous = false;
            relation = 'FORWARD_GAP';
            missingFrames = delta / expectedStep - uint64(1);
            missingTicks = delta - expectedStep;
        else
            continuous = false;
            relation = 'FORWARD_IRREGULAR';
            if delta > expectedStep
                missingTicks = delta - expectedStep;
            end
        end
    elseif current == previous
        continuous = true;
        relation = 'UNCHANGED';
        signedDelta = int64(0);
    else
        continuous = false;
        relation = 'BACKWARD';
        signedDelta = -int64(previous - current);
    end
end
