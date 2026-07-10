function radar_gui
% RADAR_GUI 雷达滑窗 Range-Doppler 动态分析仪
%   在 MATLAB 命令行中输入 radar_gui 即可启动此 GUI。
%   界面包含数据选择、元数据自动解析、核心算法参数修改输入框以及四宫格实时播放更新。

    % 清理与初始化
    clc;
    
    % 将 algorithm 子目录加入路径
    scriptDir = fileparts(mfilename('fullpath'));
    addpath(fullfile(scriptDir, 'algorithm'));

    % 状态变量
    txFile = '';
    dataDir = '';
    txMeta = [];
    rxMeta = [];
    fs = 0;
    priLen = 0;
    fc = 9.5e9;
    lambda = 299792458 / fc;
    rangePerBin = 0;
    rxChannels = [];
    selectedChannelPos = 1;
    cpiFiles = [];
    cpiFileNames = {};
    matchedFilter = [];
    refLen = 0;
    rangeZeroBin = 0;
    rangeAxis = [];
    velocityAxis = [];
    rangeAxisDisplay = [];
    velocityAxisDisplay = [];
    rangeDisplayIdx = [];
    velocityDisplayIdx = [];
    nonzeroRows = [];
    slowWindow = [];
    frameList = [];
    currentFrame = 1;
    isPlaying = false;
    playbackTimer = [];

    % 默认 DSP 和显示参数
    coherentPri = 256;
    hopPri = 4096;
    mtiStrength = 0.95;
    enableFastTimeDcRemove = true;
    displayRangeLimitsMeters = [-500, 5000];
    displayVelocityLimits = [-60, 60];
    displayDynamicRangeDb = 70;
    nonzeroVelocityGuard = 0.8;
    playDelayMs = 80;
    
    % 默认 CFAR 参数
    cfarGuardRange = 2;
    cfarGuardDoppler = 2;
    cfarTrainRange = 4;
    cfarTrainDoppler = 4;
    cfarPfaLog = -4; % 对应 Pfa = 10^-4

    % 颜色调色板 (浅色自适应/高对比度配色)
    bgColor = [0.94, 0.94, 0.94];      % 浅灰色背景
    panelColor = [0.98, 0.98, 0.98];    % 亮白色面板
    textColor = [0.08, 0.09, 0.12];    % 近黑色主文字
    secTextColor = [0.3, 0.33, 0.4];   % 深灰色辅助文字

    % 创建 UI 视窗
    fig = uifigure('Name', '雷达滑窗 Range-Doppler 动态分析仪', ...
        'Position', [100, 80, 1450, 820], 'Color', bgColor);
    fig.CloseRequestFcn = @closeFigure;
    
    % 主布局网格 (左侧控制 300px，右侧画幅自适应)
    mainGrid = uigridlayout(fig, [1, 2]);
    mainGrid.ColumnWidth = {300, '1x'};
    mainGrid.RowHeight = {'1x'};
    mainGrid.Padding = [10, 10, 10, 10];
    mainGrid.ColumnSpacing = 10;

    % 左侧控制栏容器
    sidebarPanel = uipanel(mainGrid, ...
        'Title', '控制面板 (Control Panel)', ...
        'BackgroundColor', panelColor, ...
        'ForegroundColor', textColor, ...
        'FontSize', 12, ...
        'FontWeight', 'bold');
    
    sideGrid = uigridlayout(sidebarPanel, [7, 1]);
    sideGrid.RowHeight = {80, 160, 175, 165, 45, 140, '1x'};
    sideGrid.ColumnWidth = {'1x'};
    sideGrid.Padding = [8, 8, 8, 8];
    sideGrid.RowSpacing = 12;

    % ==================== 1. 数据路径配置 ====================
    pathGrid = uigridlayout(sideGrid, [2, 2]);
    pathGrid.RowHeight = {28, 28};
    pathGrid.ColumnWidth = {85, '1x'};
    pathGrid.Padding = [0, 0, 0, 0];
    pathGrid.RowSpacing = 4;
    pathGrid.ColumnSpacing = 6;

    uibutton(pathGrid, 'Text', '选择发射...', 'ButtonPushedFcn', @selectTxFile, ...
        'BackgroundColor', [0.25, 0.32, 0.45], 'FontColor', textColor, 'FontSize', 11);
    txLabel = uieditfield(pathGrid, 'text', 'Value', '未选择发射文件 (*.bin)', 'Editable', 'off', ...
        'FontColor', secTextColor, 'FontSize', 10, 'BackgroundColor', bgColor);
    
    uibutton(pathGrid, 'Text', '选择接收...', 'ButtonPushedFcn', @selectRxDir, ...
        'BackgroundColor', [0.25, 0.32, 0.45], 'FontColor', textColor, 'FontSize', 11);
    rxLabel = uieditfield(pathGrid, 'text', 'Value', '未选择接收 Capture 目录', 'Editable', 'off', ...
        'FontColor', secTextColor, 'FontSize', 10, 'BackgroundColor', bgColor);

    % ==================== 2. 元数据只读展示 ====================
    metaPanel = uipanel(sideGrid, 'Title', '元数据配置 (Metadata)', 'BackgroundColor', panelColor, 'ForegroundColor', textColor, 'FontSize', 10);
    metaGrid = uigridlayout(metaPanel, [5, 2]);
    metaGrid.RowHeight = {22, 22, 22, 22, 26};
    metaGrid.ColumnWidth = {85, '1x'};
    metaGrid.Padding = [6, 4, 6, 4];
    metaGrid.RowSpacing = 2;

    uilabel(metaGrid, 'Text', '采样率 (Fs):', 'FontColor', secTextColor, 'FontSize', 10);
    fsValLabel = uilabel(metaGrid, 'Text', '---', 'FontColor', textColor, 'FontWeight', 'bold', 'FontSize', 10);
    
    uilabel(metaGrid, 'Text', 'PRI点数:', 'FontColor', secTextColor, 'FontSize', 10);
    priValLabel = uilabel(metaGrid, 'Text', '---', 'FontColor', textColor, 'FontWeight', 'bold', 'FontSize', 10);

    uilabel(metaGrid, 'Text', '载频 (Fc):', 'FontColor', secTextColor, 'FontSize', 10);
    fcValLabel = uilabel(metaGrid, 'Text', '---', 'FontColor', textColor, 'FontWeight', 'bold', 'FontSize', 10);

    uilabel(metaGrid, 'Text', '距离零点:', 'FontColor', secTextColor, 'FontSize', 10);
    zeroBinValLabel = uilabel(metaGrid, 'Text', '---', 'FontColor', textColor, 'FontWeight', 'bold', 'FontSize', 10);

    uilabel(metaGrid, 'Text', '处理通道:', 'FontColor', secTextColor, 'FontSize', 10);
    chDropdown = uidropdown(metaGrid, 'Items', {'未加载数据'}, 'ItemsData', 1, 'Value', 1, ...
        'ValueChangedFcn', @channelChanged, 'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);

    % ==================== 3. 核心算法参数 (输入框设计) ====================
    dspPanel = uipanel(sideGrid, 'Title', '算法核心参数 (DSP Config)', 'BackgroundColor', panelColor, 'ForegroundColor', textColor, 'FontSize', 10);
    dspGrid = uigridlayout(dspPanel, [4, 2]);
    dspGrid.RowHeight = {28, 28, 28, 28};
    dspGrid.ColumnWidth = {140, '1x'};
    dspGrid.Padding = [6, 6, 6, 6];
    dspGrid.RowSpacing = 6;

    uilabel(dspGrid, 'Text', '多普勒FFT点数 (Doppler FFT):', 'FontColor', secTextColor, 'FontSize', 10);
    coherentPriEdit = uieditfield(dspGrid, 'numeric', 'Value', coherentPri, 'Limits', [64, 1024], ...
        'RoundFractionalValues', 'on', 'ValueChangedFcn', @paramsChanged, ...
        'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);

    uilabel(dspGrid, 'Text', '滑窗步长 (Hop PRI):', 'FontColor', secTextColor, 'FontSize', 10);
    hopPriEdit = uieditfield(dspGrid, 'numeric', 'Value', hopPri, 'Limits', [128, 8192], ...
        'RoundFractionalValues', 'on', 'ValueChangedFcn', @paramsChanged, ...
        'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);

    uilabel(dspGrid, 'Text', 'MTI 去杂波强度:', 'FontColor', secTextColor, 'FontSize', 10);
    mtiStrengthEdit = uieditfield(dspGrid, 'numeric', 'Value', mtiStrength, 'Limits', [0, 1], ...
        'ValueChangedFcn', @paramsChanged, ...
        'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);

    uilabel(dspGrid, 'Text', '距离显示上限 (m):', 'FontColor', secTextColor, 'FontSize', 10);
    maxRangeEdit = uieditfield(dspGrid, 'numeric', 'Value', displayRangeLimitsMeters(2), 'Limits', [500, 20000], ...
        'RoundFractionalValues', 'on', 'ValueChangedFcn', @maxRangeChanged, ...
        'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);

    % ==================== 4. 恒虚警检测参数 (CFAR Config) ====================
    cfarPanel = uipanel(sideGrid, 'Title', 'CFAR 检测参数 (CFAR Config)', 'BackgroundColor', panelColor, 'ForegroundColor', textColor, 'FontSize', 10);
    cfarGrid = uigridlayout(cfarPanel, [3, 2]);
    cfarGrid.RowHeight = {28, 28, 28};
    cfarGrid.ColumnWidth = {140, '1x'};
    cfarGrid.Padding = [6, 6, 6, 6];
    cfarGrid.RowSpacing = 6;

    uilabel(cfarGrid, 'Text', '距离保护/训练 (G/T):', 'FontColor', secTextColor, 'FontSize', 10);
    rCfarGrid = uigridlayout(cfarGrid, [1, 2]);
    rCfarGrid.RowHeight = {'1x'};
    rCfarGrid.ColumnWidth = {'1x', '1x'};
    rCfarGrid.Padding = [0, 0, 0, 0];
    rCfarGrid.ColumnSpacing = 4;
    rGuardEdit = uieditfield(rCfarGrid, 'numeric', 'Value', cfarGuardRange, 'Limits', [0, 10], ...
        'RoundFractionalValues', 'on', 'ValueChangedFcn', @cfarParamsChanged, ...
        'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);
    rTrainEdit = uieditfield(rCfarGrid, 'numeric', 'Value', cfarTrainRange, 'Limits', [1, 20], ...
        'RoundFractionalValues', 'on', 'ValueChangedFcn', @cfarParamsChanged, ...
        'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);

    uilabel(cfarGrid, 'Text', '多普勒保护/训练 (G/T):', 'FontColor', secTextColor, 'FontSize', 10);
    dCfarGrid = uigridlayout(cfarGrid, [1, 2]);
    dCfarGrid.RowHeight = {'1x'};
    dCfarGrid.ColumnWidth = {'1x', '1x'};
    dCfarGrid.Padding = [0, 0, 0, 0];
    dCfarGrid.ColumnSpacing = 4;
    dGuardEdit = uieditfield(dCfarGrid, 'numeric', 'Value', cfarGuardDoppler, 'Limits', [0, 10], ...
        'RoundFractionalValues', 'on', 'ValueChangedFcn', @cfarParamsChanged, ...
        'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);
    dTrainEdit = uieditfield(dCfarGrid, 'numeric', 'Value', cfarTrainDoppler, 'Limits', [1, 20], ...
        'RoundFractionalValues', 'on', 'ValueChangedFcn', @cfarParamsChanged, ...
        'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);

    uilabel(cfarGrid, 'Text', '虚警概率指数 (Log10):', 'FontColor', secTextColor, 'FontSize', 10);
    pfaEdit = uieditfield(cfarGrid, 'numeric', 'Value', cfarPfaLog, 'Limits', [-12, -1], ...
        'RoundFractionalValues', 'on', 'ValueChangedFcn', @cfarParamsChanged, ...
        'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);

    % ==================== 5. 重新处理按键 ====================
    reprocessBtn = uibutton(sideGrid, 'Text', '⚡ 重新处理 (Reprocess)', 'ButtonPushedFcn', @reprocessCallback, ...
        'BackgroundColor', [0.2, 0.45, 0.8], 'FontColor', [1, 1, 1], 'FontWeight', 'bold', 'FontSize', 11);

    % ==================== 6. 播放控制与单帧延时 ====================
    playGrid = uigridlayout(sideGrid, [3, 2]);
    playGrid.RowHeight = {30, 28, 30};
    playGrid.ColumnWidth = {'1x', '1x'};
    playGrid.Padding = [0, 0, 0, 0];
    playGrid.RowSpacing = 8;
    playGrid.ColumnSpacing = 6;

    playBtn = uibutton(playGrid, 'Text', '播放 (Play)', 'ButtonPushedFcn', @togglePlay, ...
        'BackgroundColor', [0.1, 0.65, 0.4], 'FontColor', [0, 0, 0], 'FontWeight', 'bold', 'FontSize', 11);
    
    stepGrid = uigridlayout(playGrid, [1, 2]);
    stepGrid.RowHeight = {'1x'};
    stepGrid.ColumnWidth = {'1x', '1x'};
    stepGrid.Padding = [0,0,0,0];
    stepGrid.ColumnSpacing = 4;
    uibutton(stepGrid, 'Text', '◀', 'ButtonPushedFcn', @prevFrame, 'BackgroundColor', [0.85, 0.88, 0.92], 'FontColor', textColor);
    uibutton(stepGrid, 'Text', '▶', 'ButtonPushedFcn', @nextFrame, 'BackgroundColor', [0.85, 0.88, 0.92], 'FontColor', textColor);

    uilabel(playGrid, 'Text', '单帧延时 (ms):', 'FontColor', secTextColor, 'FontSize', 10, 'VerticalAlignment', 'center');
    delayEdit = uieditfield(playGrid, 'numeric', 'Value', playDelayMs, 'Limits', [10, 2000], ...
        'RoundFractionalValues', 'on', 'ValueChangedFcn', @delayChanged, ...
        'BackgroundColor', [1, 1, 1], 'FontColor', textColor, 'FontSize', 10);

    gifBtn = uibutton(playGrid, 'Text', '🎬 导出 GIF (Export GIF)', 'ButtonPushedFcn', @exportGifCallback, ...
        'BackgroundColor', [0.25, 0.45, 0.75], 'FontColor', [1, 1, 1], 'FontWeight', 'bold', 'FontSize', 11);
    gifBtn.Layout.Row = 3;
    gifBtn.Layout.Column = [1, 2];

    % ==================== 7. 滑窗播放时间轴 ====================
    timePanel = uipanel(sideGrid, 'Title', '滑窗时间轴 (Timeline)', 'BackgroundColor', panelColor, 'ForegroundColor', textColor, 'FontSize', 10);
    timeGrid = uigridlayout(timePanel, [2, 1]);
    timeGrid.RowHeight = {18, '1x'};
    timeGrid.Padding = [6, 4, 6, 4];
    timeGrid.RowSpacing = 4;

    frameInfoLabel = uilabel(timeGrid, 'Text', '未加载数据', 'FontColor', secTextColor, 'FontSize', 10, 'HorizontalAlignment', 'center');
    timelineSlider = uislider(timeGrid, 'Limits', [1, 100], 'Value', 1, 'ValueChangedFcn', @sliderChanged, ...
        'FontColor', textColor, 'FontSize', 9);

    % ==================== 右侧绘图视窗 (2x2网格，封装在 panel 中以便 GIF 捕获) ====================
    viewportPanel = uipanel(mainGrid, 'BorderType', 'none', 'BackgroundColor', bgColor);
    viewportPanel.Layout.Column = 2;
    viewportPanel.Layout.Row = 1;

    viewportGrid = uigridlayout(viewportPanel, [2, 2]);
    viewportGrid.ColumnWidth = {'1x', '1x'};
    viewportGrid.RowHeight = {'1x', '1x'};
    viewportGrid.Padding = [0, 0, 0, 0];
    viewportGrid.RowSpacing = 10;
    viewportGrid.ColumnSpacing = 10;

    % Axes 1: 无 MTI RD
    axNoMti = uiaxes(viewportGrid, 'BackgroundColor', panelColor, 'XColor', textColor, 'YColor', textColor, 'GridColor', [0.75, 0.75, 0.75]);
    title(axNoMti, '无 MTI Range-Doppler 谱图', 'Color', textColor, 'FontSize', 10, 'FontWeight', 'bold');
    
    % Axes 2: 弱 MTI RD
    axWeakMti = uiaxes(viewportGrid, 'BackgroundColor', panelColor, 'XColor', textColor, 'YColor', textColor, 'GridColor', [0.75, 0.75, 0.75]);
    title(axWeakMti, '弱 MTI Range-Doppler 谱图', 'Color', textColor, 'FontSize', 10, 'FontWeight', 'bold');
    
    % Axes 3: 零多普勒切片对比
    axZeroCut = uiaxes(viewportGrid, 'BackgroundColor', panelColor, 'XColor', textColor, 'YColor', textColor, 'GridColor', [0.75, 0.75, 0.75]);
    title(axZeroCut, '零多普勒距离切片对比', 'Color', textColor, 'FontSize', 10, 'FontWeight', 'bold');
    
    % Axes 4: 弱 MTI 非零最强切片
    axDopplerCut = uiaxes(viewportGrid, 'BackgroundColor', panelColor, 'XColor', textColor, 'YColor', textColor, 'GridColor', [0.75, 0.75, 0.75]);
    title(axDopplerCut, '最强目标多普勒切片', 'Color', textColor, 'FontSize', 10, 'FontWeight', 'bold');

    % =========================================================================
    %                             回调与核心逻辑
    % =========================================================================

    % 加载并解析发射侧元数据
    function success = loadTxMetadata()
        success = false;
        if isempty(txFile)
            return;
        end
        try
            txMetadataPath = fullfile(fileparts(txFile), 'metadata.json');
            fprintf('[GUI DEBUG] 正在加载发射元数据: %s\n', txMetadataPath);
            if ~exist(txMetadataPath, 'file')
                uialert(fig, sprintf('发射参考文件所在目录缺少 metadata.json:\n%s', txMetadataPath), '解析失败');
                return;
            end
            txMeta = read_json_file(txMetadataPath);
            fs = double(txMeta.sample_rate);
            priLen = double(txMeta.PRI);
            
            % 更新只读硬件标签展示
            fsValLabel.Text = sprintf('%.2f MHz', fs / 1e6);
            priValLabel.Text = sprintf('%d 点', priLen);
            fcValLabel.Text = sprintf('%.2f GHz', fc / 1e9);
            fprintf('[GUI DEBUG] 发射元数据加载成功: Fs = %.2f MHz, PRI = %d 点, Fc = %.2f GHz\n', fs/1e6, priLen, fc/1e9);
            success = true;
        catch ME
            fprintf('[GUI DEBUG] 发射元数据加载失败: %s\n', ME.message);
            uialert(fig, sprintf('解析发射元数据失败: %s', ME.message), '元数据错误');
        end
    end

    % 加载并解析接收侧元数据
    function success = loadRxMetadata()
        success = false;
        if isempty(dataDir)
            return;
        end
        try
            rxMetadataPath = fullfile(dataDir, 'metadata.json');
            fprintf('[GUI DEBUG] 正在加载接收元数据: %s\n', rxMetadataPath);
            if ~exist(rxMetadataPath, 'file')
                uialert(fig, sprintf('接收 capture 目录缺少 metadata.json:\n%s', rxMetadataPath), '解析失败');
                return;
            end
            rxMeta = read_json_file(rxMetadataPath);
            rxChannels = double(rxMeta.channels(:)).';
            
            % 核心逻辑：动态生成通道下拉项 (根据 metadata)
            chDropdown.Items = arrayfun(@(ch) sprintf('通道 %d (ch%d)', ch, ch), rxChannels, 'UniformOutput', false);
            chDropdown.ItemsData = 1:numel(rxChannels);
            chDropdown.Value = 1;
            selectedChannelPos = 1;
            fprintf('[GUI DEBUG] 接收元数据加载成功: 通道列表 = %s\n', mat2str(rxChannels));
            success = true;
        catch ME
            fprintf('[GUI DEBUG] 接收元数据加载失败: %s\n', ME.message);
            uialert(fig, sprintf('解析接收元数据失败: %s', ME.message), '元数据错误');
        end
    end

    % 选择发射参考文件
    function selectTxFile(~, ~)
        [filename, pathname] = uigetfile({'*.bin', 'Binary waveform (*.bin)'; '*.*', 'All Files'}, ...
            '选择发射参考 txFile (*.bin)', pwd);
        if isequal(filename, 0)
            drawnow;
            figure(fig); % 即使取消也强制拉回前台
            return;
        end
        txFile = fullfile(pathname, filename);
        txLabel.Value = txFile;
        txLabel.FontColor = textColor;
        
        initializeData();
        drawnow;
        figure(fig); % 刷新界面后强行拉回前台，防止 MATLAB Editor 抢占焦点
    end

    % 选择接收回波目录
    function selectRxDir(~, ~)
        selectedDir = uigetdir(pwd, '选择接收 capture 文件夹（包含 metadata.json 和 cpi_*.bin）');
        if isequal(selectedDir, 0)
            drawnow;
            figure(fig); % 即使取消也强制拉回前台
            return;
        end
        dataDir = selectedDir;
        rxLabel.Value = dataDir;
        rxLabel.FontColor = textColor;
        
        initializeData();
        drawnow;
        figure(fig); % 刷新界面后强行拉回前台，防止 MATLAB Editor 抢占焦点
    end

    % 数据初始化载入与自适应标定
    function initializeData()
        % 动态单步解析元数据（让界面能够即时呈现已选择的侧面参数）
        txMetaLoaded = false;
        if ~isempty(txFile)
            txMetaLoaded = loadTxMetadata();
        end
        
        rxMetaLoaded = false;
        if ~isempty(dataDir)
            rxMetaLoaded = loadRxMetadata();
        end
        
        % 只有当发射参考和接收回波都选择完毕且解析成功，才进行后续处理
        if ~txMetaLoaded || ~rxMetaLoaded
            return;
        end
        
        try
            % 停止现有播放定时器
            if ~isempty(playbackTimer) && isvalid(playbackTimer)
                stop(playbackTimer);
                delete(playbackTimer);
                playbackTimer = [];
            end
            isPlaying = false;
            playBtn.Text = '播放 (Play)';
            playBtn.BackgroundColor = [0.1, 0.65, 0.4];
            playBtn.FontColor = [0, 0, 0];

            % 准备 TX 参考滤波波形
            txNumChannels = 1;
            if isfield(txMeta, 'channels')
                txNumChannels = max(numel(txMeta.channels), 1);
            end
            txRaw = read_cs16_channel_file(txFile, txNumChannels, 1);
            if numel(txRaw) < priLen
                error('发射参考采样点数 %d 小于 TX metadata PRI 长度 %d。', numel(txRaw), priLen);
            elseif numel(txRaw) > priLen
                txRaw = txRaw(1:priLen);
            end
            activeIdx = find(abs(txRaw) > 0);
            if isempty(activeIdx)
                error('发射参考文件中没有找到非零 LFM 样本。');
            end
            txRef = txRaw(activeIdx(1):activeIdx(end));
            txRef = txRef(:);
            refLen = numel(txRef);
            txRef = txRef / sqrt(sum(abs(txRef).^2));
            matchedFilter = conj(flipud(txRef));

            % 检查接收回波 CPI 文件列表
            cpiFiles = dir(fullfile(dataDir, 'cpi_*.bin'));
            if isempty(cpiFiles)
                error('接收目录中没有找到 cpi_*.bin 文件: %s', dataDir);
            end
            [~, cpiOrder] = sort({cpiFiles.name});
            cpiFiles = cpiFiles(cpiOrder);
            cpiFileNames = {cpiFiles.name};

            % 自动标定直漏信号强反射峰的距离零点
            firstCpiFile = fullfile(dataDir, cpiFileNames{1});
            numRxChannels = numel(rxChannels);
            rangeZeroBin = calibrate_range_zero(firstCpiFile, priLen, coherentPri, ...
                numRxChannels, selectedChannelPos, matchedFilter, refLen);
            % 计算滑窗轴线和帧列表
            recalculateAxesAndFrames();
            
            % 绘制初始帧
            currentFrame = 1;
            drawFrame(currentFrame);
            
        catch ME
            uialert(fig, sprintf('数据初始化失败: %s', ME.message), '数据读取错误');
        end
    end

    % 重新计算坐标轴与滑窗帧列表 (当 coherentPri, hopPri 等参数被修改时调用)
    function recalculateAxesAndFrames()
        if isempty(txMeta) || isempty(rxMeta)
            return;
        end
        
        lambda = 299792458 / fc;
        rangePerBin = 299792458 / (2 * fs);
        
        % 更新 GUI 上的距离零点估计值
        zeroBinValLabel.Text = sprintf('%d (%.1f m)', rangeZeroBin, rangeZeroBin * rangePerBin);
        
        % 构造距离轴和速度轴 (已自动对齐零点，自 0m 起递增)
        delayBins = 0:(priLen - 1);
        rangeAxis = delayBins * rangePerBin;
        rangeDisplayMask = rangeAxis >= displayRangeLimitsMeters(1) & ...
                           rangeAxis <= displayRangeLimitsMeters(2);
        rangeDisplayIdx = find(rangeDisplayMask);
        rangeAxisDisplay = rangeAxis(rangeDisplayIdx);
        
        fdAxis = (-coherentPri/2:(coherentPri/2 - 1)) * (fs / priLen / coherentPri);
        velocityAxis = lambda * fdAxis / 2;
        
        velocityDisplayMask = velocityAxis >= displayVelocityLimits(1) & ...
                              velocityAxis <= displayVelocityLimits(2);
        velocityDisplayIdx = find(velocityDisplayMask);
        velocityAxisDisplay = velocityAxis(velocityDisplayIdx);
        
        nonzeroRows = abs(velocityAxis) >= nonzeroVelocityGuard & velocityDisplayMask;
        slowWindow = make_hann_window(coherentPri);
        
        % 预生成滑窗列表
        frameList = [];
        globalPriOffset = 0;
        bytesPerPri = priLen * numel(rxChannels) * 2 * 2;
        
        for fileCounter = 1:numel(cpiFiles)
            rxFileName = cpiFileNames{fileCounter};
            rxFile = fullfile(dataDir, rxFileName);
            
            token = regexp(rxFileName, '^cpi_(\d+)\.bin$', 'tokens', 'once');
            if isempty(token)
                cpiIndex = fileCounter - 1;
            else
                cpiIndex = str2double(token{1});
            end
            
            if ~exist(rxFile, 'file')
                continue;
            end
            
            rxInfo = dir(rxFile);
            totalPriInFile = floor(rxInfo.bytes / bytesPerPri);
            if totalPriInFile < coherentPri
                continue;
            end
            
            % 预留一个 PRI 宽度防顺延越界
            windowStarts = 1:hopPri:(totalPriInFile - coherentPri);
            for kk = 1:numel(windowStarts)
                startPri = windowStarts(kk);
                endPri = startPri + coherentPri - 1;
                centerPriInFile = startPri + (coherentPri - 1) / 2;
                centerTime = (globalPriOffset + centerPriInFile) / (fs / priLen);
                
                frameList(end+1, :) = [fileCounter, cpiIndex, startPri, endPri, centerTime]; %#ok<AGROW>
            end
            globalPriOffset = globalPriOffset + totalPriInFile;
        end
        
        if isempty(frameList)
            uialert(fig, '未能生成任何动画滑窗，请检查步长参数或回波长度。', '参数警告');
            return;
        end
        
        % 更新时间轴滑条上限
        timelineSlider.Limits = [1, size(frameList, 1)];
        timelineSlider.Value = 1;
        currentFrame = 1;
    end

    % 渲染指定帧的图表内容
    function drawFrame(frameIdx)
        if isempty(frameList) || frameIdx > size(frameList, 1)
            return;
        end
        
        cpiFilePos = frameList(frameIdx, 1);
        cpiIndex = frameList(frameIdx, 2);
        startPri = frameList(frameIdx, 3);
        endPri = frameList(frameIdx, 4);
        centerTime = frameList(frameIdx, 5);
        
        rxFile = fullfile(dataDir, cpiFileNames{cpiFilePos});
        
        fprintf('[GUI] 正在使用算法重新处理并绘制第 %d/%d 帧 (CPI: %03d, 时间: %.3f s)...\n', ...
            frameIdx, size(frameList, 1), cpiIndex, centerTime);

        % 调用独立的公共处理函数计算此滑窗的数据（传入 rangeZeroBin 以支持顺延读取）
        [rdNoMtiDb, rdWeakMtiDb, zeroNoMtiDb, zeroWeakMtiDb, ...
            weakDopplerCutDb, weakPeakRange, weakPeakVelocity, weakPeakRow] = ...
            process_one_window(rxFile, priLen, startPri, coherentPri, ...
            matchedFilter, refLen, slowWindow, mtiStrength, ...
            enableFastTimeDcRemove, rangeDisplayIdx, velocityDisplayIdx, ...
            nonzeroRows, rangeAxis, velocityAxis, numel(rxChannels), selectedChannelPos, ...
            cfarGuardRange, cfarGuardDoppler, cfarTrainRange, cfarTrainDoppler, cfarPfaLog, rangeZeroBin);
            
        % 更新 Timeline 文本标签
        frameInfoLabel.Text = sprintf('帧: %d/%d | 时间: %.3f s | CPI: %03d', ...
            frameIdx, size(frameList, 1), centerTime, cpiIndex);
        timelineSlider.Value = frameIdx;
        
        % 1. 无 MTI RD谱图绘制
        imagesc(axNoMti, rangeAxisDisplay, velocityAxisDisplay, rdNoMtiDb);
        axNoMti.YDir = 'normal';
        colormap(axNoMti, 'parula');
        clim(axNoMti, [-displayDynamicRangeDb, 0]);
        xlabel(axNoMti, '粗校正距离 (m)');
        ylabel(axNoMti, '径向速度 (m/s)');
        title(axNoMti, sprintf('无 MTI，cpi\\_%03d，PRI %d-%d', cpiIndex, startPri, endPri), 'Color', textColor);
        
        % 2. 弱 MTI RD谱图绘制 + 红圈标记
        imagesc(axWeakMti, rangeAxisDisplay, velocityAxisDisplay, rdWeakMtiDb);
        axWeakMti.YDir = 'normal';
        colormap(axWeakMti, 'parula');
        clim(axWeakMti, [-displayDynamicRangeDb, 0]);
        xlabel(axWeakMti, '粗校正距离 (m)');
        ylabel(axWeakMti, '径向速度 (m/s)');
        title(axWeakMti, sprintf('弱 MTI，R=%.1f m, v=%.2f m/s', weakPeakRange, weakPeakVelocity), 'Color', textColor);
        
        hold(axWeakMti, 'on');
        plot(axWeakMti, weakPeakRange, weakPeakVelocity, 'ro', 'MarkerSize', 6, 'MarkerFaceColor', 'r');
        hold(axWeakMti, 'off');
        
        % 3. 零多普勒距离切片
        plot(axZeroCut, rangeAxisDisplay, zeroNoMtiDb, 'Color', [0.5, 0.5, 0.5], 'LineWidth', 1.2);
        hold(axZeroCut, 'on');
        plot(axZeroCut, rangeAxisDisplay, zeroWeakMtiDb, 'r', 'LineWidth', 1.2);
        hold(axZeroCut, 'off');
        grid(axZeroCut, 'on');
        ylim(axZeroCut, [-displayDynamicRangeDb, 3]);
        xlabel(axZeroCut, '粗校正距离 (m)');
        ylabel(axZeroCut, '功率 (dB)');
        title(axZeroCut, '零多普勒距离切片对比', 'Color', textColor);
        legend(axZeroCut, {'无 MTI', '弱 MTI'}, 'TextColor', textColor, 'Color', panelColor, 'Location', 'northeast');
        
        % 4. 最强目标多普勒速度切片
        plot(axDopplerCut, velocityAxisDisplay, weakDopplerCutDb, 'b', 'LineWidth', 1.2);
        hold(axDopplerCut, 'on');
        xline(axDopplerCut, 0, 'k--', 'LineWidth', 1);
        plot(axDopplerCut, weakPeakVelocity, weakDopplerCutDb(weakPeakRow), 'ro', 'MarkerSize', 6, 'MarkerFaceColor', 'r');
        hold(axDopplerCut, 'off');
        grid(axDopplerCut, 'on');
        ylim(axDopplerCut, [-displayDynamicRangeDb, 3]);
        xlabel(axDopplerCut, '径向速度 (m/s)');
        ylabel(axDopplerCut, '功率 (dB)');
        title(axDopplerCut, sprintf('弱 MTI 多普勒切片，R=%.1f m', weakPeakRange), 'Color', textColor);
        
        drawnow;
    end

    % CFAR 参数修改回调
    function cfarParamsChanged(~, ~)
        cfarGuardRange = rGuardEdit.Value;
        cfarTrainRange = rTrainEdit.Value;
        cfarGuardDoppler = dGuardEdit.Value;
        cfarTrainDoppler = dTrainEdit.Value;
        cfarPfaLog = pfaEdit.Value;
        
        if isempty(txMeta) || isempty(rxMeta)
            return;
        end
        
        fprintf('[GUI] CFAR参数更改：保护距离=%d/训练距离=%d，保护多普勒=%d/训练多普勒=%d，Pfa=10^%d\n', ...
            cfarGuardRange, cfarTrainRange, cfarGuardDoppler, cfarTrainDoppler, cfarPfaLog);
        drawFrame(currentFrame);
    end

    % 参数修改回调
    function paramsChanged(~, ~)
        coherentPri = coherentPriEdit.Value;
        hopPri = hopPriEdit.Value;
        mtiStrength = mtiStrengthEdit.Value;
        
        if isempty(txMeta) || isempty(rxMeta)
            return;
        end
        
        fprintf('[GUI] 核心参数更改：多普勒FFT点数=%d，步长=%d，MTI强度=%.2f\n', ...
            coherentPri, hopPri, mtiStrength);
        recalculateAxesAndFrames();
        drawFrame(1);
    end

    % 距离显示上限修改回调
    function maxRangeChanged(~, ~)
        displayRangeLimitsMeters(2) = maxRangeEdit.Value;
        if isempty(txMeta) || isempty(rxMeta)
            return;
        end
        fprintf('[GUI] 距离显示上限已更改为: %d m\n', displayRangeLimitsMeters(2));
        recalculateAxesAndFrames();
        drawFrame(currentFrame);
    end

    % 重新处理按键回调
    function reprocessCallback(~, ~)
        if isempty(txMeta) || isempty(rxMeta)
            return;
        end
        
        % 同步读取最新参数
        coherentPri = coherentPriEdit.Value;
        hopPri = hopPriEdit.Value;
        mtiStrength = mtiStrengthEdit.Value;
        cfarGuardRange = rGuardEdit.Value;
        cfarTrainRange = rTrainEdit.Value;
        cfarGuardDoppler = dGuardEdit.Value;
        cfarTrainDoppler = dTrainEdit.Value;
        cfarPfaLog = pfaEdit.Value;
        displayRangeLimitsMeters(2) = maxRangeEdit.Value;
        
        fprintf('[GUI] ⚡ 重新处理：已应用最新参数并重新计算当前帧 %d...\n', currentFrame);
        recalculateAxesAndFrames();
        drawFrame(currentFrame);
    end

    % 处理通道切换回调
    function channelChanged(~, ~)
        selectedChannelPos = chDropdown.Value;
        
        if isempty(txMeta) || isempty(rxMeta)
            return;
        end
        
        % 通道切换可能导致直漏校正峰位置改变，重新校正零点
        try
            firstCpiFile = fullfile(dataDir, cpiFileNames{1});
            numRxChannels = numel(rxChannels);
            rangeZeroBin = calibrate_range_zero(firstCpiFile, priLen, coherentPri, ...
                numRxChannels, selectedChannelPos, matchedFilter, refLen);
            
            recalculateAxesAndFrames();
            drawFrame(currentFrame);
        catch ME
            uialert(fig, sprintf('通道切换失败: %s', ME.message), '通道配置错误');
        end
    end

    % 定时器播放回调
    function timerCallback(~, ~)
        if ~isPlaying || isempty(frameList)
            return;
        end
        currentFrame = currentFrame + 1;
        if currentFrame > size(frameList, 1)
            currentFrame = 1;
        end
        drawFrame(currentFrame);
    end

    % 导出 GIF 动画回调函数
    function exportGifCallback(~, ~)
        if isempty(frameList)
            uialert(fig, '未加载数据，无法导出 GIF！', '导出警告');
            return;
        end
        
        % 如果正在播放，先暂停
        if isPlaying
            togglePlay();
        end
        
        % 让用户选择保存的 GIF 文件路径，默认指向接收数据目录
        defaultPath = fullfile(dataDir, 'radar_rd_animation.gif');
        [filename, pathname] = uiputfile('*.gif', '保存 GIF 动画为', defaultPath);
        if isequal(filename, 0) || isequal(pathname, 0)
            return;
        end
        gifPath = fullfile(pathname, filename);
        
        % 弹出进度对话框
        numFrames = size(frameList, 1);
        progDlg = uiprogressdlg(fig, 'Title', '导出 GIF 动画', ...
            'Message', '正在初始化...', 'Cancelable', 'on');
        
        try
            for f = 1:numFrames
                % 检查用户是否点击了取消
                if progDlg.CancelRequested
                    fprintf('[GUI] 用户取消了 GIF 导出。\n');
                    break;
                end
                
                % 更新进度条
                progDlg.Value = f / numFrames;
                progDlg.Message = sprintf('正在处理并绘制第 %d/%d 帧...', f, numFrames);
                
                % 绘制当前帧并刷新
                drawFrame(f);
                drawnow;
                
                % 捕获整个图窗图像，并裁剪掉左侧的控制面板区域以兼容 App Designer
                frame = getframe(fig);
                im = frame2im(frame);
                
                % 自动根据屏幕 DPI 缩放比裁剪左侧 320 像素的控制栏
                imWidth = size(im, 2);
                figWidth = fig.Position(3);
                dpiScale = imWidth / figWidth;
                cropStart = max(1, round(320 * dpiScale));
                cropEnd = min(imWidth, round((figWidth - 10) * dpiScale));
                im_cropped = im(:, cropStart:cropEnd, :);
                
                [imind, cm] = rgb2ind(im_cropped, 256);
                
                % 写入 GIF 文件
                if f == 1
                    imwrite(imind, cm, gifPath, 'gif', 'Loopcount', inf, 'DelayTime', playDelayMs/1000);
                else
                    imwrite(imind, cm, gifPath, 'gif', 'WriteMode', 'append', 'DelayTime', playDelayMs/1000);
                end
            end
            
            isCancelled = progDlg.CancelRequested;
            close(progDlg);
            if ~isCancelled
                uialert(fig, sprintf('GIF 动画成功导出至：\n%s', gifPath), '导出成功', 'Icon', 'success');
            end
            
        catch ME
            if isvalid(progDlg)
                close(progDlg);
            end
            uialert(fig, sprintf('导出 GIF 失败: %s', ME.message), '导出错误');
        end
    end

    % 播放/暂停控制切换
    function togglePlay(~, ~)
        if isempty(frameList)
            return;
        end
        
        isPlaying = ~isPlaying;
        if isPlaying
            playBtn.Text = '暂停 (Pause)';
            playBtn.BackgroundColor = [0.85, 0.25, 0.25];
            playBtn.FontColor = [1, 1, 1];
            
            playbackTimer = timer('Period', playDelayMs / 1000, ...
                'ExecutionMode', 'fixedRate', ...
                'TimerFcn', @timerCallback);
            start(playbackTimer);
        else
            playBtn.Text = '播放 (Play)';
            playBtn.BackgroundColor = [0.1, 0.65, 0.4];
            playBtn.FontColor = [0, 0, 0];
            
            if ~isempty(playbackTimer) && isvalid(playbackTimer)
                stop(playbackTimer);
                delete(playbackTimer);
                playbackTimer = [];
            end
        end
    end

    % 前一帧
    function prevFrame(~, ~)
        if isempty(frameList)
            return;
        end
        if isPlaying
            togglePlay();
        end
        currentFrame = currentFrame - 1;
        if currentFrame < 1
            currentFrame = size(frameList, 1);
        end
        drawFrame(currentFrame);
    end

    % 后一帧
    function nextFrame(~, ~)
        if isempty(frameList)
            return;
        end
        if isPlaying
            togglePlay();
        end
        currentFrame = currentFrame + 1;
        if currentFrame > size(frameList, 1)
            currentFrame = 1;
        end
        drawFrame(currentFrame);
    end

    % 进度条滑动回调
    function sliderChanged(~, ~)
        if isempty(frameList)
            return;
        end
        currentFrame = round(timelineSlider.Value);
        drawFrame(currentFrame);
    end

    % 单帧播延时改动
    function delayChanged(~, ~)
        playDelayMs = delayEdit.Value;
        if isPlaying
            % 若在播放中，重置定时器周期间隔
            if ~isempty(playbackTimer) && isvalid(playbackTimer)
                stop(playbackTimer);
                delete(playbackTimer);
            end
            playbackTimer = timer('Period', playDelayMs / 1000, ...
                'ExecutionMode', 'fixedRate', ...
                'TimerFcn', @timerCallback);
            start(playbackTimer);
        end
    end

    % 关闭窗口清理定时器
    function closeFigure(~, ~)
        if ~isempty(playbackTimer) && isvalid(playbackTimer)
            stop(playbackTimer);
            delete(playbackTimer);
        end
        delete(fig);
    end
end
