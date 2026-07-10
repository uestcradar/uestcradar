function run_gui()
%RUN_GUI Radar signal processing GUI.
%
% Run from MATLAB with:
%   run_gui

this_dir = fileparts(mfilename('fullpath'));
ROOT     = fileparts(this_dir);
addpath(this_dir);
addpath(fullfile(this_dir, 'callbacks'));
addpath(genpath(fullfile(ROOT, 'src')));

%% ===== 涓荤獥鍙?=====
hFig = uifigure('Name','闆疯揪淇″彿澶勭悊绯荤粺 v2', 'Position',[20 10 1480 1060]);

%% ===== 宸︿晶鎺у埗闈㈡澘 =====
hLeft = uipanel(hFig, 'Position',[6 42 400 1010], ...
    'BackgroundColor',[0.95 0.95 0.96], 'BorderType','line');

y  = 980;
DY = 24;
DS = 6;

%% ===== 鏂囦欢澶归槦鍒?=====
y = sec_label(hLeft, '鏂囦欢澶归槦鍒?, y);
hFolderList = uilistbox(hLeft, ...
    'Position',[8 y-100 384 104], ...
    'Items',   {}, ...
    'FontName','Consolas', 'FontSize',10);
y = y - 108;

uibutton(hLeft,'Text','+ 娣诲姞鏂囦欢澶?,'Position',[8 y 122 24], ...
    'ButtonPushedFcn',@(~,~) add_folder(hFig, hFolderList));
uibutton(hLeft,'Text','- 鍒犻櫎閫変腑','Position',[134 y 100 24], ...
    'ButtonPushedFcn',@(~,~) remove_folder(hFig, hFolderList));
uibutton(hLeft,'Text','鑷姩濉厖鍙傛暟','Position',[238 y 154 24], ...
    'BackgroundColor',[0.2 0.5 0.8],'FontColor','w', ...
    'ButtonPushedFcn',@(~,~) auto_fill_params(hFig));
y = y - DS*2;

%% ===== 姝ラ閫夐」 =====
y = sec_label(hLeft, '姝ラ閫夐」', y);
hDoParse   = uicheckbox(hLeft,'Text','閲嶆柊瑙ｆ瀽 bin 鏂囦欢锛堝凡鏈夊垯璺宠繃锛?, ...
    'Position',[12 y 360 22],'Value',true);
y = y - DY;
hDoProcess = uicheckbox(hLeft,'Text','閲嶆柊鐢熸垚 RD 绔嬫柟浣擄紙淇濈暀鏃ф枃浠讹級', ...
    'Position',[12 y 360 22],'Value',true);
y = y - DY;
hDoPlot    = uicheckbox(hLeft,'Text','閲嶆柊鐢熸垚 GIF / 妫€娴?mat', ...
    'Position',[12 y 360 22],'Value',true);
y = y - DY - DS;

%% ===== 棰勫鐞嗗弬鏁?=====
y = sec_label(hLeft, '棰勫鐞?, y);
hFastDC  = uicheckbox(hLeft,'Text','蹇椂闂村幓鐩存祦锛堟秷闄DC鐩存祦鍋忕疆锛?, ...
    'Position',[12 y 360 22],'Value',true); y = y - DY;

y = sec_label(hLeft, '鐩磋揪娉㈠鐞?, y);
hDWCalib = uicheckbox(hLeft,'Text','鑷姩瀹氫綅锛坈alibrate_range_zero锛?, ...
    'Position',[12 y 360 22],'Value',true); y = y - DY;
uilabel(hLeft,'Text','  鎵嬪姩 dw_bin锛堣嚜鍔ㄥ叧闂椂鐢級:','Position',[12 y 190 20]);
hDWManual = uieditfield(hLeft,'numeric','Position',[206 y 70 22],'Value',854);
y = y - DY;
hDWBlank = uicheckbox(hLeft,'Text','鏃跺煙娑堥殣锛堟寲鍘荤洿杈炬尝鍖洪棿锛?, ...
    'Position',[12 y 360 22],'Value',true); y = y - DY;
hSubsamp = uicheckbox(hLeft,'Text','浜氶噰鏍风簿瀵归綈锛堟姏鐗╃嚎鍐呮彃锛?, ...
    'Position',[12 y 360 22],'Value',true); y = y - DY;
hFreqComp = uicheckbox(hLeft,'Text','鎱㈡椂闂撮鍋忚ˉ鍋?, ...
    'Position',[12 y 360 22],'Value',true); y = y - DY - DS;

%% ===== MTI / CPI =====
y = sec_label(hLeft, 'MTI / CPI 鍙傛暟', y);
hMtiMean = uicheckbox(hLeft,'Text','鎱㈡椂闂村潎鍊肩浉鍑忥紙浼?=1.0 鍥哄畾锛?, ...
    'Position',[12 y 360 22],'Value',true); y = y - DY;
hMti2P   = uicheckbox(hLeft,'Text','涓よ剦鍐插娑?, ...
    'Position',[12 y 360 22],'Value',true); y = y - DY;
uilabel(hLeft,'Text','N_cpi:',    'Position',[12 y 50 20]);
hNCpi = uieditfield(hLeft,'numeric','Position',[66 y 58 22],'Value',512);
uilabel(hLeft,'Text','N_overlap:','Position',[132 y 68 20]);
hNOverlap = uieditfield(hLeft,'numeric','Position',[204 y 58 22],'Value',256);
y = y - DY;
uilabel(hLeft,'Text','鏈€澶ц窛绂?(m):','Position',[12 y 80 20]);
hMaxRange = uieditfield(hLeft,'numeric','Position',[96 y 72 22],'Value',2000);
y = y - DY - DS;

%% ===== 鏄剧ず鍙傛暟 =====
y = sec_label(hLeft, '鏄剧ず鍙傛暟', y);
uilabel(hLeft,'Text','璺濈鑼冨洿(m):','Position',[12 y 80 20]);
hRMin = uieditfield(hLeft,'numeric','Position',[96 y 58 22],'Value',300);
uilabel(hLeft,'Text','~','Position',[158 y 12 20]);
hRMax = uieditfield(hLeft,'numeric','Position',[174 y 58 22],'Value',800);
y = y - DY;
uilabel(hLeft,'Text','閫熷害鑼冨洿(m/s):','Position',[12 y 90 20]);
hVMin = uieditfield(hLeft,'numeric','Position',[106 y 56 22],'Value',-50);
uilabel(hLeft,'Text','~','Position',[166 y 12 20]);
hVMax = uieditfield(hLeft,'numeric','Position',[182 y 56 22],'Value',50);
y = y - DY;
uilabel(hLeft,'Text','鑹查樁 dB锛堢粷瀵癸級锛?,'Position',[12 y 100 20]);
hClimLo = uieditfield(hLeft,'numeric','Position',[116 y 58 22],'Value',110);
uilabel(hLeft,'Text','~','Position',[178 y 12 20]);
hClimHi = uieditfield(hLeft,'numeric','Position',[194 y 58 22],'Value',155);
y = y - DY;
uilabel(hLeft,'Text','GIF 鎶藉抚闂撮殧:','Position',[12 y 88 20]);
hFrameStepGif = uieditfield(hLeft,'numeric','Position',[104 y 58 22],'Value',20, ...
    'Limits',[1 Inf],'Tooltip','1=閫愬抚锛堟參锛夛紱20=姣?0甯у彇1甯э紙蹇級');
uilabel(hLeft,'Text','甯?,'Position',[168 y 24 20]);
y = y - DY - DS;

%% ===== CFAR 鍙傛暟 =====
y = sec_label(hLeft, 'CFAR 鍙傛暟', y);
uilabel(hLeft,'Text','guard_r:','Position',[12 y 54 20]);
hCfarGr = uieditfield(hLeft,'numeric','Position',[70 y 44 22],'Value',2);
uilabel(hLeft,'Text','guard_d:','Position',[122 y 54 20]);
hCfarGd = uieditfield(hLeft,'numeric','Position',[180 y 44 22],'Value',4);
y = y - DY;
uilabel(hLeft,'Text','ref_r:','Position',[12 y 44 20]);
hCfarRr = uieditfield(hLeft,'numeric','Position',[60 y 44 22],'Value',8);
uilabel(hLeft,'Text','ref_d:','Position',[112 y 44 20]);
hCfarRd = uieditfield(hLeft,'numeric','Position',[160 y 44 22],'Value',16);
y = y - DY;
uilabel(hLeft,'Text','PFA:','Position',[12 y 34 20]);
hCfarPfa = uieditfield(hLeft,'numeric','Position',[50 y 80 22],'Value',1e-4, ...
    'LowerLimitInclusive','off','Limits',[0 1]);
y = y - DY - DS;

%% ===== DBSCAN 鍙傛暟 =====
y = sec_label(hLeft, 'DBSCAN 鍙傛暟', y);
uilabel(hLeft,'Text','eps锛堟牸锛?','Position',[12 y 64 20]);
hDbEps = uieditfield(hLeft,'numeric','Position',[80 y 50 22],'Value',3);
uilabel(hLeft,'Text','min_pts:','Position',[140 y 54 20]);
hDbMin = uieditfield(hLeft,'numeric','Position',[198 y 50 22],'Value',3, ...
    'Limits',[1 Inf]);
y = y - DY - DS;

%% ===== 娴嬭鑼冨洿杩囨护 =====
y = sec_label(hLeft, '娴嬭鑼冨洿杩囨护锛堜粎瀵硅鑼冨洿鍐呯洰鏍囨祴瑙掞級', y);
uilabel(hLeft,'Text','璺濈(m):','Position',[12 y 58 20]);
hAngRLo = uieditfield(hLeft,'numeric','Position',[74 y 56 22],'Value',300);
uilabel(hLeft,'Text','~','Position',[134 y 12 20]);
hAngRHi = uieditfield(hLeft,'numeric','Position',[150 y 56 22],'Value',800);
y = y - DY;
uilabel(hLeft,'Text','閫熷害(m/s):','Position',[12 y 64 20]);
hAngVLo = uieditfield(hLeft,'numeric','Position',[80 y 56 22],'Value',-50);
uilabel(hLeft,'Text','~','Position',[140 y 12 20]);
hAngVHi = uieditfield(hLeft,'numeric','Position',[156 y 56 22],'Value',50);
y = y - DY - DS;

%% ===== 鎿嶄綔鎸夐挳 =====
hBtnRun = uibutton(hLeft,'Text','鈻?澶勭悊鎵€鏈夋枃浠跺す', ...
    'Position',[8 y 190 36], ...
    'BackgroundColor',[0.10 0.44 0.10],'FontColor','w', ...
    'FontSize',13,'FontWeight','bold');
hBtnGIF = uibutton(hLeft,'Text','馃攽  淇濆瓨涓夌被 GIF', ...
    'Position',[202 y 190 36], ...
    'BackgroundColor',[0.12 0.28 0.60],'FontColor','w', ...
    'FontSize',13,'Enable','off');

%% ===== 鐘舵€佹爮 =====
hStatus = uilabel(hFig,'Text','灏辩华 - 娣诲姞鏂囦欢澶瑰悗鐐瑰嚮"澶勭悊鎵€鏈夋枃浠跺す"', ...
    'Position',[6 6 1468 28], ...
    'BackgroundColor',[0.08 0.08 0.08],'FontColor',[0.20 0.90 0.20], ...
    'FontSize',10,'FontName','Consolas');

%% ===== 鍙充晶鏄剧ず鍖哄煙 =====
hRight = uipanel(hFig,'Position',[412 42 1060 1010],'BorderType','none');

uilabel(hRight,'Text','褰撳墠缁撴灉:','Position',[8 980 60 22]);
hResPath = uieditfield(hRight,'text','Position',[72 980 850 24],'Value','锛堝皻鏈鐞嗭級');
uibutton(hRight,'Text','娴忚','Position',[926 980 60 24], ...
    'ButtonPushedFcn',@(~,~) browse_result(hFig,hResPath));
uibutton(hRight,'Text','鍔犺浇','Position',[990 980 60 24], ...
    'ButtonPushedFcn',@(~,~) load_result(hFig,hResPath));

uilabel(hRight,'Text','鏄剧ず:','Position',[8 948 36 22]);
hViewMode = uidropdown(hRight, ...
    'Items',{'RD 鍥?,'CFAR+鑱氱被','娴嬭'}, ...
    'Position',[48 948 110 24],'Value','RD 鍥?);
uilabel(hRight,'Text','閫氶亾:','Position',[166 948 40 22]);
hChSel = uidropdown(hRight, ...
    'Items',{'ch0 鍜岄€氶亾','ch1 鏂瑰悜宸?,'ch2 浠拌宸?}, ...
    'Position',[210 948 130 24],'Value','ch0 鍜岄€氶亾');
uilabel(hRight,'Text','甯?','Position',[350 948 28 22]);
hFrameEdit  = uieditfield(hRight,'numeric','Position',[380 948 56 22],'Value',1,'Limits',[1 Inf]);
hFrameTotal = uilabel(hRight,'Text','/ 0','Position',[440 948 52 22]);
uibutton(hRight,'Text','鈼€','Position',[496 948 30 24], ...
    'ButtonPushedFcn',@(~,~) cb_show_frame(hFig, round(hFrameEdit.Value)-1));
uibutton(hRight,'Text','鈻?,'Position',[530 948 30 24], ...
    'ButtonPushedFcn',@(~,~) cb_show_frame(hFig, round(hFrameEdit.Value)+1));
hBtnPlay = uibutton(hRight,'Text','鈻舵挱鏀?,'Position',[566 948 74 24], ...
    'ButtonPushedFcn',@(~,~) cb_play_step(hFig));
uilabel(hRight,'Text','姝ラ暱:','Position',[648 948 36 22]);
hPlayStep = uieditfield(hRight,'numeric','Position',[688 948 44 22],'Value',1,'Limits',[1 Inf]);
uilabel(hRight,'Text','闂撮殧(s):','Position',[740 948 56 22]);
hPlayPeriod = uieditfield(hRight,'numeric','Position',[800 948 50 22],'Value',0.1, ...
    'Limits',[0.02 10]);

hAx = uiaxes(hRight,'Position',[8 8 1042 932]);
xlabel(hAx,'寰勫悜閫熷害 (m/s)'); ylabel(hAx,'鏂滆窛 (m)');
title(hAx,'绛夊緟澶勭悊鎴栧姞杞界粨鏋?); colormap(hAx,jet);
cb = colorbar(hAx); cb.Label.String = '骞呭害 (dB, 缁濆鍊?';

%% ===== UserData 鍒濆鍖?=====
hFig.UserData.processing   = false;
hFig.UserData.is_playing   = false;
hFig.UserData.folder_paths = {};
hFig.UserData.mf_rd        = [];
hFig.UserData.rd_mat_path  = '';
hFig.UserData.r_axis       = [];
hFig.UserData.v_axis       = [];
hFig.UserData.n_frames     = 0;
hFig.UserData.ch_var_names = {'RD_Sum_All','RD_Az_All','RD_El_All'};
hFig.UserData.root         = ROOT;
hFig.UserData.det_result   = [];
hFig.UserData.ang_result   = [];
hFig.UserData.ax           = hAx;
hFig.UserData.status       = hStatus;
hFig.UserData.frame_edit   = hFrameEdit;
hFig.UserData.frame_total  = hFrameTotal;
hFig.UserData.btn_gif      = hBtnGIF;
hFig.UserData.btn_play     = hBtnPlay;
hFig.UserData.h_folder_list = hFolderList;
hFig.UserData.h_res_path    = hResPath;
hFig.UserData.h_do_parse    = hDoParse;
hFig.UserData.h_do_process  = hDoProcess;
hFig.UserData.h_do_plot     = hDoPlot;
hFig.UserData.h_fast_dc     = hFastDC;
hFig.UserData.h_dw_calib    = hDWCalib;
hFig.UserData.h_dw_manual   = hDWManual;
hFig.UserData.h_dw_blank    = hDWBlank;
hFig.UserData.h_subsamp     = hSubsamp;
hFig.UserData.h_freq_comp   = hFreqComp;
hFig.UserData.h_mti_mean    = hMtiMean;
hFig.UserData.h_mti_2p      = hMti2P;
hFig.UserData.h_ncpi        = hNCpi;
hFig.UserData.h_noverlap    = hNOverlap;
hFig.UserData.h_max_range   = hMaxRange;
hFig.UserData.h_r_min       = hRMin;
hFig.UserData.h_r_max       = hRMax;
hFig.UserData.h_v_min       = hVMin;
hFig.UserData.h_v_max       = hVMax;
hFig.UserData.h_clim_lo     = hClimLo;
hFig.UserData.h_clim_hi     = hClimHi;
hFig.UserData.h_frame_step_gif = hFrameStepGif;
hFig.UserData.h_ch_sel      = hChSel;
hFig.UserData.h_view_mode   = hViewMode;
hFig.UserData.h_play_step   = hPlayStep;
hFig.UserData.h_play_period = hPlayPeriod;
hFig.UserData.h_cfar_gr     = hCfarGr;
hFig.UserData.h_cfar_gd     = hCfarGd;
hFig.UserData.h_cfar_rr     = hCfarRr;
hFig.UserData.h_cfar_rd     = hCfarRd;
hFig.UserData.h_cfar_pfa    = hCfarPfa;
hFig.UserData.h_db_eps      = hDbEps;
hFig.UserData.h_db_min      = hDbMin;
hFig.UserData.h_ang_r_lo    = hAngRLo;
hFig.UserData.h_ang_r_hi    = hAngRHi;
hFig.UserData.h_ang_v_lo    = hAngVLo;
hFig.UserData.h_ang_v_hi    = hAngVHi;

%% ===== 鏃ュ織绐楀彛 =====
hLogFig  = uifigure('Name','澶勭悊鏃ュ織', 'Position',[1515 460 480 580], ...
    'CloseRequestFcn', @(f,~) set(f,'Visible','off'));
hLogArea = uitextarea(hLogFig, 'Position',[5 36 470 540], ...
    'Editable',false, 'FontName','Microsoft YaHei', 'FontSize',10);
uibutton(hLogFig,'Text','娓呴櫎', 'Position',[5 5 70 26], ...
    'ButtonPushedFcn', @(~,~) set(hLogArea,'Value',{''}));
uibutton(hLogFig,'Text','澶嶅埗鍏ㄩ儴', 'Position',[82 5 80 26], ...
    'ButtonPushedFcn', @(~,~) clipboard('copy', strjoin(hLogArea.Value, newline)));
hFig.UserData.log_fig  = hLogFig;
hFig.UserData.log_area = hLogArea;

%% ===== 鍥炶皟缁戝畾 =====
hBtnRun.ButtonPushedFcn    = @(~,~) cb_run(hFig);
hBtnGIF.ButtonPushedFcn    = @(~,~) cb_save_gif(hFig);
hFrameEdit.ValueChangedFcn = @(s,~) cb_show_frame(hFig, round(s.Value));
hChSel.ValueChangedFcn     = @(~,~) cb_show_frame(hFig, round(hFrameEdit.Value));
hViewMode.ValueChangedFcn  = @(~,~) cb_show_frame(hFig, round(hFrameEdit.Value));

uibutton(hFig,'Text','鏄剧ず鏃ュ織', 'Position',[6 6 80 26], ...
    'ButtonPushedFcn', @(~,~) show_log(hFig));
end

function y2 = sec_label(parent, txt, y)
uilabel(parent,'Text',txt,'Position',[4 y 392 22], ...
    'FontWeight','bold','FontSize',11, ...
    'BackgroundColor',[0.72 0.80 0.92]);
y2 = y - 26;
end

function add_folder(hFig, hList)
d = uigetdir('', '閫夋嫨鏁版嵁鏂囦欢澶?);
drawnow;
if isvalid(hFig)
    hFig.WindowState = 'normal';
end
if isequal(d, 0), return; end
items = hList.Items;
if ~any(strcmp(items, d))
    hList.Items = [items, {d}];
    hFig.UserData.folder_paths = hList.Items;
end
end

function remove_folder(hFig, hList)
sel = hList.Value;
if isempty(sel), return; end
items = hList.Items;
hList.Items = items(~strcmp(items, sel));
hFig.UserData.folder_paths = hList.Items;
end

function auto_fill_params(hFig)
ud  = hFig.UserData;
sel = ud.h_folder_list.Value;
if isempty(sel)
    ud.status.Text = '璇峰厛鍦ㄦ枃浠跺す闃熷垪涓€変腑涓€涓枃浠跺す';
    return;
end
data_dir = sel;
if iscell(data_dir), data_dir = data_dir{1}; end
try
    cfg = build_config(data_dir, struct(), ud.root);
    p = cfg_to_legacy_params(cfg);
    ud.h_ncpi.Value      = p.N_cpi;
    ud.h_noverlap.Value  = p.N_overlap;
    ud.h_max_range.Value = p.max_range;
    ud.h_dw_manual.Value = p.dw_bin_manual;
    ud.status.Text = sprintf('[鑷姩鍙傛暟] %s -> Fs=%.2fMHz priLen=%d N_cpi=%d max_range=%dm', ...
        p.dataset, p.fs/1e6, p.priLen, p.N_cpi, p.max_range);
catch ME
    ud.status.Text = sprintf('[鑷姩鍙傛暟 澶辫触] %s', ME.message);
end
end

function show_log(hFig)
ud = hFig.UserData;
if isfield(ud,'log_fig') && isvalid(ud.log_fig)
    ud.log_fig.Visible = 'on';
    figure(ud.log_fig);
end
end

function browse_result(hFig, hResPath)
[f, d] = uigetfile({'RD_Proc_*.mat','RD绔嬫柟浣撴枃浠?}, '閫夋嫨 RD_Proc_*.mat', ...
    hFig.UserData.root);
if isequal(f,0), return; end
hResPath.Value = fullfile(d, f);
end

function load_result(hFig, hResPath)
mat_path = hResPath.Value;
if ~exist(mat_path,'file')
    hFig.UserData.status.Text = sprintf('[閿欒] 鏂囦欢涓嶅瓨鍦? %s', mat_path);
    return;
end
hFig.UserData.rd_mat_path  = mat_path;
hFig.UserData.mf_rd        = matfile(mat_path);
hFig.UserData.r_axis       = double(hFig.UserData.mf_rd.r_axis_full);
hFig.UserData.v_axis       = double(hFig.UserData.mf_rd.v_axis_full);
hFig.UserData.n_frames     = double(hFig.UserData.mf_rd.total_blocks);
hFig.UserData.frame_total.Text  = sprintf('/ %d', hFig.UserData.n_frames);
hFig.UserData.frame_edit.Limits = [1, hFig.UserData.n_frames];
hFig.UserData.btn_gif.Enable    = 'on';

res_dir = fileparts(mat_path);
det_files = dir(fullfile(res_dir, '*_det.mat'));
ang_files = dir(fullfile(res_dir, '*_angle.mat'));
hFig.UserData.det_result = [];
hFig.UserData.ang_result = [];
if ~isempty(det_files)
    [~,ii] = max([det_files.datenum]);
    try
        tmp = load(fullfile(res_dir, det_files(ii).name), 'det_result');
        hFig.UserData.det_result = tmp.det_result;
        hFig.UserData.status.Text = sprintf('宸插姞杞?%d 甯э紙鍚娴嬬粨鏋滐級锛?s', ...
            hFig.UserData.n_frames, mat_path);
    catch
    end
end
if ~isempty(ang_files)
    [~,ii] = max([ang_files.datenum]);
    try
        tmp = load(fullfile(res_dir, ang_files(ii).name), 'ang_result');
        hFig.UserData.ang_result = tmp.ang_result;
    catch
    end
end
if isempty(hFig.UserData.det_result)
    hFig.UserData.status.Text = sprintf('宸插姞杞?%d 甯э紙鏃犳娴?mat锛屽皢瀹炴椂璁＄畻锛夛細%s', ...
        hFig.UserData.n_frames, mat_path);
end
cb_show_frame(hFig, 1);
end




