function analyze_bin_gui
%ANALYZE_BIN_GUI Validate and convert one continuous CYHD BIN capture.

    bgColor = [0.95, 0.96, 0.98];
    panelColor = [1.00, 1.00, 1.00];
    textColor = [0.10, 0.12, 0.16];
    secondaryColor = [0.35, 0.38, 0.45];
    selectedBinPath = '';
    cachedRecordInfo = struct([]);

    fig = uifigure( ...
        'Name', 'CYHD BIN 校验与保存', ...
        'Position', [180, 90, 920, 700], ...
        'Color', bgColor);

    rootGrid = uigridlayout(fig, [5, 1]);
    rootGrid.RowHeight = {64, 66, 28, 42, '1x'};
    rootGrid.Padding = [14, 14, 14, 14];
    rootGrid.RowSpacing = 10;

    pathPanel = uipanel(rootGrid, ...
        'Title', '选择连续录制 BIN 文件', ...
        'BackgroundColor', panelColor, ...
        'ForegroundColor', textColor, ...
        'FontWeight', 'bold');
    pathGrid = uigridlayout(pathPanel, [1, 2]);
    pathGrid.ColumnWidth = {130, '1x'};
    pathGrid.Padding = [8, 4, 8, 4];
    pathGrid.ColumnSpacing = 8;

    selectButton = uibutton(pathGrid, 'push', ...
        'Text', '选择 BIN 文件...', ...
        'ButtonPushedFcn', @selectFile, ...
        'BackgroundColor', [0.18, 0.42, 0.76], ...
        'FontColor', [1, 1, 1], ...
        'FontWeight', 'bold');
    pathField = uieditfield(pathGrid, 'text', ...
        'Value', '尚未选择文件', ...
        'Editable', 'off', ...
        'FontColor', secondaryColor, ...
        'BackgroundColor', [0.98, 0.98, 0.98]);

    actionPanel = uipanel(rootGrid, ...
        'Title', '操作', ...
        'BackgroundColor', panelColor, ...
        'ForegroundColor', textColor, ...
        'FontWeight', 'bold');
    actionGrid = uigridlayout(actionPanel, [1, 5]);
    actionGrid.ColumnWidth = {70, 130, '1x', 150, 150};
    actionGrid.Padding = [8, 4, 8, 4];
    actionGrid.ColumnSpacing = 8;

    uilabel(actionGrid, ...
        'Text', 'PRI点数:', ...
        'HorizontalAlignment', 'right', ...
        'FontColor', textColor, ...
        'FontWeight', 'bold');
    priField = uieditfield(actionGrid, 'numeric', ...
        'Value', 4096, ...
        'Limits', [1, Inf], ...
        'RoundFractionalValues', 'on', ...
        'ValueDisplayFormat', '%.0f');
    uilabel(actionGrid, ...
        'Text', 'BIN内固定；保存矩阵为 [慢时间PRI × 快时间点]', ...
        'FontColor', secondaryColor);
    validateButton = uibutton(actionGrid, 'push', ...
        'Text', '校验文件', ...
        'Enable', 'off', ...
        'ButtonPushedFcn', @runValidation, ...
        'BackgroundColor', [0.20, 0.55, 0.34], ...
        'FontColor', [1, 1, 1], ...
        'FontWeight', 'bold');
    saveButton = uibutton(actionGrid, 'push', ...
        'Text', '保存数据 MAT', ...
        'Enable', 'off', ...
        'ButtonPushedFcn', @runSave, ...
        'BackgroundColor', [0.82, 0.47, 0.10], ...
        'FontColor', [1, 1, 1], ...
        'FontWeight', 'bold');

    progressGauge = uigauge(rootGrid, 'linear', ...
        'Limits', [0, 100], ...
        'Value', 0, ...
        'MajorTicks', [0, 25, 50, 75, 100]);

    statusLabel = uilabel(rootGrid, ...
        'Text', '等待选择 BIN 文件', ...
        'FontColor', secondaryColor, ...
        'FontSize', 12, ...
        'FontWeight', 'bold');

    logPanel = uipanel(rootGrid, ...
        'Title', '运行信息', ...
        'BackgroundColor', panelColor, ...
        'ForegroundColor', textColor, ...
        'FontWeight', 'bold');
    logGrid = uigridlayout(logPanel, [1, 1]);
    logGrid.Padding = [8, 8, 8, 8];
    logArea = uitextarea(logGrid, ...
        'Editable', 'off', ...
        'Value', {'请选择一个 BIN 文件。'}, ...
        'FontName', 'Monospaced', ...
        'FontSize', 11, ...
        'BackgroundColor', [0.985, 0.985, 0.985], ...
        'FontColor', textColor);

    function selectFile(~, ~)
        [fileName, fileDir] = uigetfile( ...
            {'*.bin', 'Binary capture (*.bin)'; '*.*', 'All files'}, ...
            '选择需要检查的连续 BIN 文件', pwd);
        if isequal(fileName, 0)
            return;
        end

        selectedBinPath = fullfile(fileDir, fileName);
        cachedRecordInfo = struct([]);
        pathField.Value = selectedBinPath;
        pathField.FontColor = textColor;
        logArea.Value = {sprintf('已选择: %s', selectedBinPath), ...
            '请选择“校验文件”或填写PRI后点击“保存数据 MAT”。'};
        progressGauge.Value = 0;
        statusLabel.Text = '文件已选择，尚未执行校验或保存';
        statusLabel.FontColor = secondaryColor;
        validateButton.Enable = 'on';
        saveButton.Enable = 'on';
    end

    function runValidation(~, ~)
        if isempty(selectedBinPath)
            return;
        end
        outputPath = getReportPath();
        if isfile(outputPath) && ~confirmOverwrite({outputPath}, '确认覆盖校验报告')
            return;
        end

        setBusy(true);
        cleanup = onCleanup(@() setBusy(false));
        try
            record_info = scanSelectedFile();
            atomicSave(outputPath, record_info);
            emitLine(sprintf('报告已保存: %s', outputPath));
            setStatusFromRecord(record_info, '校验完成');
            progressGauge.Value = 100;
        catch ME
            statusLabel.Text = '校验失败';
            statusLabel.FontColor = [0.78, 0.12, 0.12];
            emitLine(sprintf('校验失败: %s', ME.message));
            uialert(fig, sprintf('BIN 校验失败：\n%s', ME.message), '校验失败');
        end
    end

    function runSave(~, ~)
        if isempty(selectedBinPath)
            return;
        end
        priSamples = priField.Value;
        setBusy(true);
        cleanup = onCleanup(@() setBusy(false));

        try
            if cacheMatchesSelectedFile()
                record_info = cachedRecordInfo;
                emitLine('使用当前文件已完成的校验结果。');
            else
                emitLine('保存前未找到可复用校验结果，先自动校验。');
                record_info = scanSelectedFile();
            end

            if record_info.summary.valid_frame_count == 0
                error('CYHD:NoValidFrame', '没有可保存的有效帧。');
            end

            outputDir = fileparts(selectedBinPath);
            targetPaths = getDataTargetPaths(record_info, priSamples, outputDir);
            if isempty(targetPaths)
                error('CYHD:NoCompletePri', '所有连续段都不足一个完整PRI。');
            end
            reportPath = getReportPath();
            overwriteCandidates = [{reportPath}; targetPaths(:)];
            existingMask = cellfun(@isfile, overwriteCandidates);
            if any(existingMask) && ...
                    ~confirmOverwrite(overwriteCandidates(existingMask), '确认覆盖已有MAT文件')
                emitLine('用户取消保存。');
                return;
            end

            progressGauge.Value = 0;
            save_summary = cyhd_internal.save_bin_data_mat(selectedBinPath, record_info, priSamples, ...
                'OutputDir', outputDir, ...
                'Overwrite', true, ...
                'ProgressFcn', @updateProgress, ...
                'LogFcn', @emitLine);
            atomicSave(reportPath, record_info);
            printSaveSummary(save_summary, record_info, reportPath);
            statusLabel.Text = sprintf('保存完成：%s个数据文件', ...
                formatUint(save_summary.data_file_count));
            statusLabel.FontColor = [0.08, 0.52, 0.22];
            progressGauge.Value = 100;
        catch ME
            statusLabel.Text = '保存失败';
            statusLabel.FontColor = [0.78, 0.12, 0.12];
            emitLine(sprintf('保存失败: %s', ME.message));
            uialert(fig, sprintf('数据保存失败：\n%s', ME.message), '保存失败');
        end
    end

    function info = scanSelectedFile()
        progressGauge.Value = 0;
        statusLabel.Text = '正在校验...';
        statusLabel.FontColor = [0.15, 0.35, 0.70];
        drawnow;
        info = cyhd_internal.scan_bin_record_info(selectedBinPath, ...
            'ProgressFcn', @updateProgress, ...
            'LogFcn', @emitLine);
        cachedRecordInfo = info;
        printReport(info);
    end

    function matched = cacheMatchesSelectedFile()
        matched = false;
        if isempty(cachedRecordInfo) || ~isfield(cachedRecordInfo, 'file') || ...
                ~isfile(selectedBinPath)
            return;
        end
        entry = dir(selectedBinPath);
        matched = uint64(entry(1).bytes) == cachedRecordInfo.file.size_bytes && ...
            strcmp(entry(1).date, cachedRecordInfo.file.modified_time);
    end

    function setBusy(busy)
        if ~isvalid(fig)
            return;
        end
        if busy
            state = 'off';
        else
            state = 'on';
        end
        selectButton.Enable = state;
        priField.Enable = state;
        if isempty(selectedBinPath)
            validateButton.Enable = 'off';
            saveButton.Enable = 'off';
        else
            validateButton.Enable = state;
            saveButton.Enable = state;
        end
        drawnow;
    end

    function outputPath = getReportPath()
        [outputDir, stem] = fileparts(selectedBinPath);
        outputPath = fullfile(outputDir, [stem, '_record_info.mat']);
    end

    function paths = getDataTargetPaths(info, priSamples, outputDir)
        [~, stem] = fileparts(selectedBinPath);
        paths = {};
        for row = 1:height(info.segments)
            sampleCount = info.segments.frame_count(row) * ...
                uint64(info.protocol.iq_payload_points);
            if sampleCount >= uint64(priSamples)
                paths{end + 1, 1} = fullfile(outputDir, sprintf( ...
                    '%s_data_seg%03d.mat', stem, info.segments.segment_id(row))); %#ok<AGROW>
            end
        end
    end

    function confirmed = confirmOverwrite(paths, titleText)
        pathText = strjoin(paths, newline);
        answer = uiconfirm(fig, sprintf('以下文件已存在，是否覆盖？\n%s', pathText), ...
            titleText, ...
            'Options', {'覆盖', '取消'}, ...
            'DefaultOption', 2, ...
            'CancelOption', 2);
        confirmed = strcmp(answer, '覆盖');
    end

    function setStatusFromRecord(info, prefix)
        statusLabel.Text = sprintf('%s：%s', prefix, info.summary.status);
        switch info.summary.status
            case 'PASS'
                statusLabel.FontColor = [0.08, 0.52, 0.22];
            case 'WARN'
                statusLabel.FontColor = [0.80, 0.48, 0.05];
            otherwise
                statusLabel.FontColor = [0.78, 0.12, 0.12];
        end
    end

    function updateProgress(fraction, message)
        if ~isvalid(fig)
            return;
        end
        progressGauge.Value = min(max(fraction * 100, 0), 100);
        statusLabel.Text = sprintf('%s  %.1f%%', message, fraction * 100);
        drawnow limitrate;
    end

    function emitLine(message)
        message = char(message);
        fprintf('%s\n', message);
        if ~isvalid(fig)
            return;
        end
        values = logArea.Value;
        if ischar(values)
            values = {values};
        end
        values{end + 1, 1} = message;
        logArea.Value = values;
        drawnow limitrate;
    end

    function printReport(info)
        emitLine('');
        emitLine('================ BIN 扫描报告 ================');
        emitLine(sprintf('文件: %s', info.file.full_path));
        emitLine(sprintf('大小: %s bytes (%.3f MiB)', ...
            formatUint(info.file.size_bytes), double(info.file.size_bytes) / 1024 / 1024));
        emitLine(sprintf('耗时: %.3f s', info.file.scan_duration_seconds));
        emitLine(sprintf('结论: %s', info.summary.status));
        emitLine(sprintf('候选帧 / 成帧 / 完全有效: %s / %s / %s', ...
            formatUint(info.summary.frame_candidates), ...
            formatUint(info.summary.framed_frame_count), ...
            formatUint(info.summary.valid_frame_count)));
        emitLine(sprintf('坏帧: %s；连续有效段: %s', ...
            formatUint(info.summary.bad_frame_count), ...
            formatUint(info.summary.continuous_segment_count)));
        emitLine(sprintf('不连续点: %s；推断缺失帧周期: %s；无法推断断点: %s', ...
            formatUint(info.summary.gap_count), ...
            formatUint(info.summary.inferred_missing_frame_count), ...
            formatUint(info.summary.unresolved_gap_count)));
        emitLine(sprintf('推断缺失总时长: %.6f秒（%s）', ...
            info.summary.inferred_missing_duration_seconds, ...
            formatDuration(info.summary.inferred_missing_duration_seconds)));
        emitLine(sprintf('内部结构问题: %s；边界残片: %s；语义疑点: %s', ...
            formatUint(info.summary.structural_issue_count), ...
            formatUint(info.summary.boundary_issue_count), ...
            formatUint(info.summary.semantic_issue_count)));
        emitLine(sprintf('BEAM错误 / 数据头错误 / 保留区错误 / 帧尾错误: %s / %s / %s / %s', ...
            formatUint(info.structural.beam_magic_error_count), ...
            formatUint(info.structural.data_header_error_count), ...
            formatUint(info.structural.beam_reserved_error_count), ...
            formatUint(info.structural.tail_error_count)));
        emitLine('CRC/状态区域: 按实际录制协议保留，本工具不校验。');
        emitLine(sprintf('跳过字节: %s；残缺尾帧字节: %s；重同步: %s', ...
            formatUint(info.structural.skipped_bytes), ...
            formatUint(info.structural.truncated_tail_bytes), ...
            formatUint(info.structural.resync_count)));
        emitLine(sprintf('波位元数据字节序: %s；出现波位数: %d', ...
            info.structural.metadata_byte_order, height(info.beam_summary)));
        emitLine(sprintf('sweep/pulse/beam语义异常: %s / %s / %s；时间戳回退: %s', ...
            formatUint(info.semantic.sweep_count.jump_forward_count + info.semantic.sweep_count.backward_nonzero_count), ...
            formatUint(info.semantic.pulse_in_beam.jump_forward_count + info.semantic.pulse_in_beam.backward_nonzero_count), ...
            formatUint(info.semantic.current_beam_idx.jump_forward_count + info.semantic.current_beam_idx.backward_nonzero_count), ...
            formatUint(info.semantic.timestamp_backward_count)));

        if ~isempty(info.gaps)
            emitLine('不连续处时间推断：');
            for row = 1:height(info.gaps)
                emitLine(sprintf(['  断点%d时间戳：前帧=%s（0x%016X），', ...
                    '后帧=%s（0x%016X），差值=%s tick。'], ...
                    info.gaps.gap_id(row), ...
                    formatUint(info.gaps.previous_timestamp(row)), ...
                    info.gaps.previous_timestamp(row), ...
                    formatUint(info.gaps.next_timestamp(row)), ...
                    info.gaps.next_timestamp(row), ...
                    formatUint(info.gaps.timestamp_delta_ticks(row))));
                if info.gaps.inference_reliable(row)
                    emitLine(sprintf(['  断点%d（段%d→%d，帧%s→%s）：缺失%s帧周期，', ...
                        '缺失时长%.6f秒（%s），物理异常区%s字节。'], ...
                        info.gaps.gap_id(row), info.gaps.previous_segment_id(row), ...
                        info.gaps.next_segment_id(row), ...
                        formatUint(info.gaps.previous_frame_index(row)), ...
                        formatUint(info.gaps.next_frame_index(row)), ...
                        formatUint(info.gaps.inferred_missing_frame_count(row)), ...
                        info.gaps.inferred_missing_duration_seconds(row), ...
                        formatDuration(info.gaps.inferred_missing_duration_seconds(row)), ...
                        formatUint(info.gaps.physical_gap_bytes(row))));
                else
                    emitLine(sprintf('  断点%d（段%d→%d）：无法可靠推断；%s', ...
                        info.gaps.gap_id(row), info.gaps.previous_segment_id(row), ...
                        info.gaps.next_segment_id(row), info.gaps.note{row}));
                end
            end
        end

        issueCount = height(info.issues);
        if issueCount > 0
            emitLine(sprintf('异常明细（显示前%d/%d条，MAT中保存全部）：', min(issueCount, 100), issueCount));
            for row = 1:min(issueCount, 100)
                emitLine(sprintf('  [%s][%s] offset=%s frame=%s %s', ...
                    info.issues.severity{row}, info.issues.type{row}, ...
                    formatUint(info.issues.byte_offset(row)), ...
                    formatUint(info.issues.frame_index(row)), ...
                    info.issues.detail{row}));
            end
        else
            emitLine('异常明细: 无');
        end
        emitLine('==============================================');
    end

    function printSaveSummary(summary, info, reportPath)
        emitLine('');
        emitLine('================ MAT 保存报告 ================');
        emitLine(sprintf('保存目录: %s', summary.output_dir));
        emitLine(sprintf('数据MAT数量: %s', formatUint(summary.data_file_count)));
        emitLine(sprintf('总保存有效XDMA帧: %s；总完整PRI: %s', ...
            formatUint(summary.saved_frame_count), ...
            formatUint(summary.saved_pri_count)));
        emitLine(sprintf('跳过坏帧: %s；不连续点: %s；PRI尾部丢弃采样: %s', ...
            formatUint(summary.skipped_bad_frame_count), ...
            formatUint(info.summary.gap_count), ...
            formatUint(summary.discarded_sample_count)));
        for row = 1:height(summary.files)
            emitLine(sprintf(['  段%d: %s帧，%s个PRI，矩阵[%s × %u]，', ...
                '尾部丢弃%s点。'], ...
                summary.files.segment_id(row), ...
                formatUint(summary.files.frame_count(row)), ...
                formatUint(summary.files.pri_count(row)), ...
                formatUint(summary.files.pri_count(row)), ...
                uint32(priField.Value), ...
                formatUint(summary.files.discarded_sample_count(row))));
            emitLine(sprintf('    %s', summary.files.path{row}));
        end
        emitLine(sprintf('质量报告: %s', reportPath));
        emitLine('==============================================');
    end
end

function atomicSave(outputPath, record_info)
    outputDir = fileparts(outputPath);
    temporaryPath = [tempname(outputDir), '.mat'];
    cleanup = onCleanup(@() deleteIfExists(temporaryPath));
    save(temporaryPath, 'record_info', '-v7.3');
    [ok, message] = movefile(temporaryPath, outputPath, 'f');
    if ~ok
        error('CYHD:SaveFailed', '无法保存报告: %s', message);
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

function text = formatDuration(seconds)
    if ~isfinite(seconds)
        text = '不可用';
        return;
    end
    hours = floor(seconds / 3600);
    minutes = floor(mod(seconds, 3600) / 60);
    remainingSeconds = mod(seconds, 60);
    text = sprintf('%02d:%02d:%09.6f', hours, minutes, remainingSeconds);
end
