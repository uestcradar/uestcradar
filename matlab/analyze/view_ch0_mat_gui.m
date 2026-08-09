function view_ch0_mat_gui
%VIEW_CH0_MAT_GUI Plot CH0 time-domain and STFT images from a converted MAT.

    bgColor = [0.95, 0.96, 0.98];
    panelColor = [1, 1, 1];
    textColor = [0.10, 0.12, 0.16];
    secondaryColor = [0.35, 0.38, 0.45];
    selectedMatPath = '';

    fig = uifigure( ...
        'Name', 'CYHD CH0 时域 / STFT 查看器', ...
        'Position', [100, 60, 1240, 820], ...
        'Color', bgColor);

    rootGrid = uigridlayout(fig, [3, 1]);
    rootGrid.RowHeight = {72, 30, '1x'};
    rootGrid.Padding = [12, 12, 12, 12];
    rootGrid.RowSpacing = 8;

    controlPanel = uipanel(rootGrid, ...
        'Title', '选择解析后的数据 MAT', ...
        'BackgroundColor', panelColor, ...
        'ForegroundColor', textColor, ...
        'FontWeight', 'bold');
    controlGrid = uigridlayout(controlPanel, [1, 6]);
    controlGrid.ColumnWidth = {145, '1x', 90, 110, 135, 135};
    controlGrid.Padding = [8, 5, 8, 5];
    controlGrid.ColumnSpacing = 8;

    selectButton = uibutton(controlGrid, 'push', ...
        'Text', '选择数据 MAT...', ...
        'ButtonPushedFcn', @selectMatFile, ...
        'BackgroundColor', [0.18, 0.42, 0.76], ...
        'FontColor', [1, 1, 1], ...
        'FontWeight', 'bold');
    pathField = uieditfield(controlGrid, 'text', ...
        'Value', '尚未选择文件', ...
        'Editable', 'off', ...
        'FontColor', secondaryColor, ...
        'BackgroundColor', [0.98, 0.98, 0.98]);
    uilabel(controlGrid, ...
        'Text', '前N个PRI:', ...
        'HorizontalAlignment', 'right', ...
        'FontColor', textColor, ...
        'FontWeight', 'bold');
    pulseCountField = uieditfield(controlGrid, 'numeric', ...
        'Value', 32, ...
        'Limits', [1, Inf], ...
        'RoundFractionalValues', 'on', ...
        'ValueDisplayFormat', '%.0f');
    plotButton = uibutton(controlGrid, 'push', ...
        'Text', '读取并绘图', ...
        'Enable', 'off', ...
        'ButtonPushedFcn', @plotSelectedData, ...
        'BackgroundColor', [0.20, 0.55, 0.34], ...
        'FontColor', [1, 1, 1], ...
        'FontWeight', 'bold');
    timestampButton = uibutton(controlGrid, 'push', ...
        'Text', '校验时间戳', ...
        'Enable', 'off', ...
        'ButtonPushedFcn', @validateTimestamps, ...
        'BackgroundColor', [0.82, 0.47, 0.10], ...
        'FontColor', [1, 1, 1], ...
        'FontWeight', 'bold');

    statusLabel = uilabel(rootGrid, ...
        'Text', '请选择由 BIN 解析生成的 *_data_segXXX.mat 文件。', ...
        'FontColor', secondaryColor, ...
        'FontWeight', 'bold');

    plotGrid = uigridlayout(rootGrid, [2, 1]);
    plotGrid.RowHeight = {'1x', '1x'};
    plotGrid.RowSpacing = 10;
    timeAxes = uiaxes(plotGrid);
    stftAxes = uiaxes(plotGrid);
    configureAxes(timeAxes, 'CH0 时域幅度');
    configureAxes(stftAxes, 'CH0 STFT');

    function selectMatFile(~, ~)
        [fileName, fileDir] = uigetfile( ...
            {'*_data_seg*.mat', '解析数据 MAT (*_data_seg*.mat)'; ...
            '*.mat', 'MAT files (*.mat)'; '*.*', 'All files'}, ...
            '选择解析后的数据 MAT 文件', pwd);
        if isequal(fileName, 0)
            return;
        end
        selectedMatPath = fullfile(fileDir, fileName);
        pathField.Value = selectedMatPath;
        pathField.FontColor = textColor;
        plotButton.Enable = 'on';
        timestampButton.Enable = 'on';
        statusLabel.Text = '文件已选择，可以绘图或校验全部时间戳。';
        statusLabel.FontColor = secondaryColor;
    end

    function validateTimestamps(~, ~)
        if isempty(selectedMatPath)
            return;
        end

        setBusy(true);
        cleanup = onCleanup(@() setBusy(false));
        try
            statusLabel.Text = '正在逐条校验全部时间戳...';
            statusLabel.FontColor = [0.15, 0.35, 0.70];
            drawnow;
            report = cyhd_internal.validate_mat_timestamps(selectedMatPath, ...
                'ProgressFcn', @updateTimestampProgress);
            printTimestampReport(report);
            if strcmp(report.status, 'PASS')
                statusLabel.Text = sprintf('时间戳连续：已检查%s条。', ...
                    formatUint(report.timestamp_count));
                statusLabel.FontColor = [0.08, 0.52, 0.22];
            else
                statusLabel.Text = sprintf('时间戳不连续：发现%s处异常。', ...
                    formatUint(report.discontinuity_count));
                statusLabel.FontColor = [0.78, 0.12, 0.12];
            end
        catch ME
            statusLabel.Text = '时间戳校验失败';
            statusLabel.FontColor = [0.78, 0.12, 0.12];
            fprintf('[时间戳校验] 失败: %s\n', ME.message);
            uialert(fig, sprintf('时间戳校验失败：\n%s', ME.message), '校验失败');
        end
    end

    function updateTimestampProgress(fraction, message)
        if ~isvalid(fig)
            return;
        end
        statusLabel.Text = sprintf('%s %.1f%%', message, fraction * 100);
        drawnow limitrate;
    end

    function printTimestampReport(report)
        fprintf('\n================ MAT 时间戳校验报告 ================\n');
        fprintf('文件: %s\n', report.file_path);
        fprintf('结论: %s\n', report.status);
        fprintf('帧时间戳条目 / 相邻帧转换: %s / %s\n', ...
            formatUint(report.timestamp_count), formatUint(report.transition_count));
        fprintf('合法变化: 保持不变（0 tick）或前进%s tick\n', ...
            formatUint(report.expected_advance_ticks));
        fprintf('首时间戳: %s（0x%016X）\n', ...
            formatUint(report.first_timestamp), report.first_timestamp);
        fprintf('末时间戳: %s（0x%016X）\n', ...
            formatUint(report.last_timestamp), report.last_timestamp);
        fprintf('不连续总数: %s\n', formatUint(report.discontinuity_count));
        fprintf('合法保持不变 / 合法前进4096: %s / %s\n', ...
            formatUint(report.unchanged_transition_count), ...
            formatUint(report.advanced_4096_transition_count));
        fprintf('前向整帧缺口 / 非4096前跳 / 回退: %s / %s / %s\n', ...
            formatUint(report.forward_gap_count), ...
            formatUint(report.irregular_forward_count), ...
            formatUint(report.backward_count));
        fprintf('推断缺失帧: %s；前向缺失tick: %s；缺失时长: %.9f秒\n', ...
            formatUint(report.inferred_missing_frame_count), ...
            formatUint(report.forward_missing_ticks), ...
            report.forward_missing_duration_seconds);
        fprintf('耗时: %.3f秒（仅分块读取时间戳，未读取通道数据）\n', ...
            report.scan_duration_seconds);

        if report.discontinuity_count == 0
            fprintf('异常明细: 无\n');
        else
            fprintf('异常明细（显示前%s/%s条）：\n', ...
                formatUint(report.stored_issue_count), ...
                formatUint(report.discontinuity_count));
            for row = 1:height(report.issues)
                fprintf(['  [%s] 帧 %s→%s: 前=%s（0x%016X），', ...
                    '后=%s（0x%016X），差值=%d tick，推断缺失帧=%s\n'], ...
                    report.issues.type{row}, ...
                    formatUint(report.issues.current_frame_index(row) - uint64(1)), ...
                    formatUint(report.issues.current_frame_index(row)), ...
                    formatUint(report.issues.previous_timestamp(row)), ...
                    report.issues.previous_timestamp(row), ...
                    formatUint(report.issues.current_timestamp(row)), ...
                    report.issues.current_timestamp(row), ...
                    report.issues.signed_delta_ticks(row), ...
                    formatUint(report.issues.inferred_missing_frame(row)));
            end
            if report.issues_truncated
                fprintf('  其余异常未逐条打印，但已计入上述统计。\n');
            end
        end
        fprintf('======================================================\n\n');
    end

    function plotSelectedData(~, ~)
        if isempty(selectedMatPath)
            return;
        end

        setBusy(true);
        cleanup = onCleanup(@() setBusy(false));
        try
            statusLabel.Text = '正在局部读取 CH0...';
            statusLabel.FontColor = [0.15, 0.35, 0.70];
            drawnow;
            preview = cyhd_internal.read_ch0_mat_preview(selectedMatPath, pulseCountField.Value);
            ch0 = preview.ch0;
            pulseCount = double(preview.loaded_pulses);
            fs = preview.sample_rate;

            continuousSignal = reshape(ch0.', [], 1);
            continuousTimeMilliseconds = (0:(numel(continuousSignal) - 1)) / fs * 1e3;
            plot(timeAxes, continuousTimeMilliseconds, abs(continuousSignal), ...
                'Color', [0.08, 0.34, 0.72], 'LineWidth', 0.8);
            grid(timeAxes, 'on');
            xlabel(timeAxes, '连续时间 (ms)');
            ylabel(timeAxes, '|CH0|（原始幅值）');
            title(timeAxes, sprintf('CH0 前 %d 个 PRI 的连续一维时域幅度', pulseCount));
            if numel(continuousTimeMilliseconds) > 1
                xlim(timeAxes, [continuousTimeMilliseconds(1), continuousTimeMilliseconds(end)]);
            end

            [spectrum, frequency, time] = calculateStft(continuousSignal, fs);
            stftMagnitudeDb = normalizedDb(abs(spectrum));
            imagesc(stftAxes, time * 1e3, frequency / 1e6, stftMagnitudeDb);
            axis(stftAxes, 'xy');
            xlabel(stftAxes, '连续时间 (ms)');
            ylabel(stftAxes, '频率 (MHz)');
            title(stftAxes, sprintf('CH0 前 %d 个 PRI 的 STFT（归一化 dB）', pulseCount));
            colorbar(stftAxes);
            colormap(stftAxes, turbo(256));
            clim(stftAxes, [-80, 0]);

            if preview.loaded_pulses < preview.requested_pulses
                statusText = sprintf('请求%s个PRI，文件仅有%s个，已全部绘制。', ...
                    formatUint(preview.requested_pulses), ...
                    formatUint(preview.available_pulses));
            else
                statusText = sprintf('绘制完成：%s个PRI，CH0矩阵[%s × %u]。', ...
                    formatUint(preview.loaded_pulses), ...
                    formatUint(preview.loaded_pulses), preview.pri_samples);
            end
            statusLabel.Text = statusText;
            statusLabel.FontColor = [0.08, 0.52, 0.22];
            fprintf('[CH0查看器] %s 文件=%s\n', statusText, selectedMatPath);
        catch ME
            statusLabel.Text = '读取或绘图失败';
            statusLabel.FontColor = [0.78, 0.12, 0.12];
            fprintf('[CH0查看器] 失败: %s\n', ME.message);
            uialert(fig, sprintf('读取或绘图失败：\n%s', ME.message), '处理失败');
        end
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
        pulseCountField.Enable = state;
        if isempty(selectedMatPath)
            plotButton.Enable = 'off';
            timestampButton.Enable = 'off';
        else
            plotButton.Enable = state;
            timestampButton.Enable = state;
        end
        drawnow;
    end
end

function configureAxes(ax, titleText)
    ax.Color = [1, 1, 1];
    ax.XGrid = 'on';
    ax.YGrid = 'on';
    ax.Box = 'on';
    title(ax, titleText);
end

function [spectrum, frequency, time] = calculateStft(signal, sampleRate)
    if numel(signal) < 16
        error('CYHD:InsufficientSamples', '至少需要16个CH0采样点才能计算STFT。');
    end
    maximumWindow = min([512, numel(signal)]);
    windowLength = 2 ^ floor(log2(maximumWindow));
    window = hann(windowLength, 'periodic');
    overlapLength = floor(windowLength * 0.75);
    fftLength = max(512, 2 ^ nextpow2(windowLength));
    [spectrum, frequency, time] = spectrogram(signal, window, ...
        overlapLength, fftLength, sampleRate, 'centered');
end

function valuesDb = normalizedDb(magnitude)
    peak = max(magnitude(:));
    if peak == 0
        valuesDb = -80 * ones(size(magnitude), 'single');
        return;
    end
    valuesDb = 20 * log10(single(magnitude) / single(peak) + eps('single'));
    valuesDb = max(valuesDb, single(-80));
end

function text = formatUint(value)
    text = sprintf('%u', uint64(value));
end
