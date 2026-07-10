function cb_play_step(hFig)
%CB_PLAY_STEP GUI 回调：切换播放/暂停
%
% 使用 while 循环 + drawnow 替代 timer，避免 uifigure 下 timer 后台线程
% 与 UI 渲染冲突导致只走一帧就停止的问题。
% 点击"暂停"时设置 is_playing=false，while 循环在当前帧结束后退出。

ud = hFig.UserData;

%% 若正在播放 → 停止
if isfield(ud,'is_playing') && ud.is_playing
    hFig.UserData.is_playing = false;
    return;
end

%% 开始播放
hFig.UserData.is_playing = true;
ud.btn_play.Text = '⏸ 暂停';
drawnow;

try
    while isvalid(hFig) && hFig.UserData.is_playing
        ud     = hFig.UserData;
        step   = max(1, round(ud.h_play_step.Value));
        period = max(0.02, ud.h_play_period.Value);

        k = round(ud.frame_edit.Value) + step;
        if k > ud.n_frames, k = 1; end

        cb_show_frame(hFig, k);

        % 等待帧间隔，同时持续处理 UI 事件（让暂停按钮能响应）
        t0 = tic;
        while toc(t0) < period
            drawnow('limitrate');
            if ~isvalid(hFig) || ~hFig.UserData.is_playing, break; end
            pause(0.01);
        end
    end
catch ME
    % 帧显示出错时不让 is_playing 卡住，在命令行打印诊断信息
    fprintf('[cb_play_step] 播放出错（帧%d）: %s\n', ...
        round(hFig.UserData.frame_edit.Value), ME.message);
end

%% 播放结束后恢复按钮文字（无论正常退出还是出错都会执行）
if isvalid(hFig)
    hFig.UserData.is_playing = false;
    if isvalid(hFig.UserData.btn_play)
        hFig.UserData.btn_play.Text = '▷ 播放';
    end
end
end
