function cb_run(hFig)
%CB_RUN Process all queued folders from the GUI.

ud = hFig.UserData;
addpath(genpath(fullfile(ud.root, 'src')));
if ud.processing
    gui_status(hFig, 'Processing is already running.');
    return;
end
hFig.UserData.processing = true;

folder_list = ud.h_folder_list.Items;
if isempty(folder_list)
    gui_status(hFig, '[error] No data folders selected.');
    hFig.UserData.processing = false;
    return;
end

last_out_file = '';

try
    for di = 1:numel(folder_list)
        data_dir = folder_list{di};
        [~, ds] = fileparts(data_dir);
        gui_status(hFig, sprintf('[%d/%d] %s', di, numel(folder_list), ds));
        drawnow;

        cfg = build_config(data_dir, build_gui_overrides(ud), ud.root);
        if ~ud.h_do_process.Value
            cfg.dev.force_reprocess = false;
        end
        t0 = tic;
        result = run_dataset_pipeline(cfg, struct( ...
            'status_cb', @(msg) gui_status(hFig, sprintf('[%s] %s', ds, msg)), ...
            'force_reprocess', cfg.dev.force_reprocess));
        out_file = result.out_file;
        gui_status(hFig, sprintf('[%s] Pipeline done in %.1f min', ds, toc(t0) / 60));
        drawnow;
        last_out_file = out_file;
        drawnow;
    end

    if ~isempty(last_out_file)
        load_last_result(hFig, last_out_file);
        gui_status(hFig, sprintf('All done, loaded %d frames', hFig.UserData.n_frames));
    else
        gui_status(hFig, 'All done with no valid output.');
    end
catch ME
    gui_status(hFig, sprintf('[error] %s - %s', ME.identifier, ME.message));
end

hFig.UserData.processing = false;
end

function overrides = build_gui_overrides(ud)
overrides = struct();
overrides.pipeline = struct();
stage_list = {'load_parsed', 'calibrate_direct_wave', 'range_doppler', 'detect', 'angle'};
if ud.h_do_parse.Value
    stage_list = [{'parse'}, stage_list];
end
if ud.h_do_plot.Value
    stage_list{end + 1} = 'export_plot';
end
overrides.pipeline.stages = stage_list;

overrides.process = struct();
overrides.process.fast_dc_remove = ud.h_fast_dc.Value;
overrides.process.direct_wave_calibrate = ud.h_dw_calib.Value;
overrides.process.dw_bin_manual = round(ud.h_dw_manual.Value);
overrides.process.direct_wave_blank = ud.h_dw_blank.Value;
overrides.process.subsample_align = ud.h_subsamp.Value;
overrides.process.freq_compensation = ud.h_freq_comp.Value;
overrides.process.mti_mean = ud.h_mti_mean.Value;
overrides.process.mti_two_pulse = ud.h_mti_2p.Value;

overrides.cpi = struct();
overrides.cpi.n_cpi = round(ud.h_ncpi.Value);
overrides.cpi.n_overlap = round(ud.h_noverlap.Value);
overrides.cpi.n_guard = 10;

overrides.radar = struct();
overrides.radar.max_range_m = ud.h_max_range.Value;

overrides.display = struct();
overrides.display.range_m = [ud.h_r_min.Value, ud.h_r_max.Value];
overrides.display.velocity_mps = [ud.h_v_min.Value, ud.h_v_max.Value];
overrides.display.clim_db = [ud.h_clim_lo.Value, ud.h_clim_hi.Value];
overrides.display.gif_frame_step = round(ud.h_frame_step_gif.Value);

overrides.detect = struct();
overrides.detect.cfar = struct();
overrides.detect.cfar.guard_r = ud.h_cfar_gr.Value;
overrides.detect.cfar.guard_d = ud.h_cfar_gd.Value;
overrides.detect.cfar.ref_r = ud.h_cfar_rr.Value;
overrides.detect.cfar.ref_d = ud.h_cfar_rd.Value;
overrides.detect.cfar.pfa = ud.h_cfar_pfa.Value;
overrides.detect.dbscan = struct();
overrides.detect.dbscan.eps = ud.h_db_eps.Value;
overrides.detect.dbscan.min_pts = ud.h_db_min.Value;

overrides.angle = struct();
overrides.angle.range_m = [ud.h_ang_r_lo.Value, ud.h_ang_r_hi.Value];
overrides.angle.velocity_mps = [ud.h_ang_v_lo.Value, ud.h_ang_v_hi.Value];
end

function load_last_result(hFig, last_out_file)
hFig.UserData.rd_mat_path = last_out_file;
hFig.UserData.mf_rd = matfile(last_out_file);
hFig.UserData.r_axis = double(hFig.UserData.mf_rd.r_axis_full);
hFig.UserData.v_axis = double(hFig.UserData.mf_rd.v_axis_full);
hFig.UserData.n_frames = double(hFig.UserData.mf_rd.total_blocks);
hFig.UserData.frame_total.Text = sprintf('/ %d', hFig.UserData.n_frames);
hFig.UserData.frame_edit.Limits = [1, hFig.UserData.n_frames];
hFig.UserData.btn_gif.Enable = 'on';
hFig.UserData.h_res_path.Value = last_out_file;

res_dir = fileparts(last_out_file);
hFig.UserData.det_result = [];
hFig.UserData.ang_result = [];

det_files = dir(fullfile(res_dir, '*_det.mat'));
ang_files = dir(fullfile(res_dir, '*_angle.mat'));

if ~isempty(det_files)
    [~, ii] = max([det_files.datenum]);
    try
        tmp = load(fullfile(res_dir, det_files(ii).name), 'det_result');
        hFig.UserData.det_result = tmp.det_result;
    catch
    end
end

if ~isempty(ang_files)
    [~, ii] = max([ang_files.datenum]);
    try
        tmp = load(fullfile(res_dir, ang_files(ii).name), 'ang_result');
        hFig.UserData.ang_result = tmp.ang_result;
    catch
    end
end

cb_show_frame(hFig, 1);
end

function gui_status(hFig, msg)
if ~isvalid(hFig)
    return;
end

ud = hFig.UserData;
if isfield(ud, 'status') && isvalid(ud.status)
    ud.status.Text = msg;
end
if isfield(ud, 'log_area') && ~isempty(ud.log_area) && isvalid(ud.log_area)
    ts = datestr(now, 'HH:MM:SS');
    line = sprintf('[%s] %s', ts, msg);
    old = ud.log_area.Value;
    if isempty(old) || (iscell(old) && numel(old) == 1 && isempty(old{1}))
        ud.log_area.Value = {line};
    else
        if ~iscell(old)
            old = {old};
        end
        ud.log_area.Value = [old; {line}];
    end
    scroll(ud.log_area, 'bottom');
end
drawnow;
end
