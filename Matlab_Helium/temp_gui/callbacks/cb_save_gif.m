function cb_save_gif(hFig)
%CB_SAVE_GIF Export GIFs for the currently loaded RD result.

ud = hFig.UserData;
if isempty(ud.mf_rd)
    ud.status.Text = '[GIF] 请先处理或加载结果文件';
    return;
end

result_dir = fileparts(ud.rd_mat_path);
if isempty(result_dir)
    result_dir = ud.root;
end
res_dir = fullfile(result_dir, 'GIF_from_GUI');
if ~exist(res_dir, 'dir')
    mkdir(res_dir);
end

opts.r_range    = [ud.h_r_min.Value, ud.h_r_max.Value];
opts.v_range    = [ud.h_v_min.Value, ud.h_v_max.Value];
opts.clim_dB    = [ud.h_clim_lo.Value, ud.h_clim_hi.Value];
opts.frame_step = round(ud.h_frame_step_gif.Value);
opts.status_cb  = @(msg) set_status(hFig, msg);

try
    radar_plot(ud.rd_mat_path, res_dir, opts);
    set_status(hFig, sprintf('[GIF] 已保存至: %s', res_dir));
catch ME
    set_status(hFig, sprintf('[GIF 错误] %s', ME.message));
end
end

function set_status(hFig, msg)
if isvalid(hFig) && isvalid(hFig.UserData.status)
    hFig.UserData.status.Text = msg;
    drawnow;
end
end
