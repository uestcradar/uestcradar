function cb_show_frame(hFig, k)
%CB_SHOW_FRAME 在 GUI 坐标轴显示第 k 帧（支持 RD / CFAR+聚类 / 测角 三种模式）

ud = hFig.UserData;
if isempty(ud.mf_rd) || ud.n_frames == 0, return; end

k = max(1, min(ud.n_frames, k));
ud.frame_edit.Value = k;

% 显示范围
r_min = ud.h_r_min.Value;  r_max = ud.h_r_max.Value;
v_min = ud.h_v_min.Value;  v_max = ud.h_v_max.Value;
r_mask = ud.r_axis >= r_min & ud.r_axis <= r_max;
v_mask = ud.v_axis >= v_min & ud.v_axis <= v_max;
r_disp = ud.r_axis(r_mask);
v_disp = ud.v_axis(v_mask);
clim_lo = ud.h_clim_lo.Value;
clim_hi = ud.h_clim_hi.Value;
ax = ud.ax;

mode = ud.h_view_mode.Value;

switch mode
    % ----------------------------------------------------------------
    case 'RD 图'
        ch_idx = ud.h_ch_sel.ValueIndex;
        ch_var = ud.ch_var_names{ch_idx};
        rd_slice = ud.mf_rd.(ch_var)(:, :, k);
        rd_dB    = 20 * log10(abs(rd_slice(r_mask, v_mask)) + eps);

        % 还原可能被 CFAR 模式改掉的坐标轴颜色
        set(ax, 'Color','w', 'XColor','k', 'YColor','k');
        imagesc(ax, v_disp, r_disp, rd_dB, [clim_lo, clim_hi]);
        ax.YDir = 'normal';
        colormap(ax, jet);
        cb = colorbar(ax); cb.Label.String = '幅度 (dB, 绝对值)';
        xlabel(ax, '径向速度 (m/s)'); ylabel(ax, '斜距 (m)');
        ch_names = {'和通道(ch0)', '方位差(ch1)', '俯仰差(ch2)'};
        title(ax, sprintf('帧 %d/%d  |  %s  |  %g ~ %g dB', ...
            k, ud.n_frames, ch_names{ch_idx}, clim_lo, clim_hi), ...
            'Interpreter','none');

    % ----------------------------------------------------------------
    case 'CFAR+聚类'
        % 优先使用预加载的 det_result。
        % 注意：det_r 按抽帧序号(fi)索引，不是实际帧号(k)；
        % 须用 frame_ids 找到离 k 最近的 fi，避免大帧号或抽帧>1时索引越界/取错数据。
        if ~isempty(ud.det_result)
            dr = ud.det_result;
            if isfield(dr,'frame_ids') && ~isempty(dr.frame_ids)
                [~, fi] = min(abs(double(dr.frame_ids) - k));
            else
                fi = min(k, numel(dr.det_r));   % 旧格式兼容（frame_step==1时fi==k）
            end
            det_r_idx = double(dr.det_r{fi});
            det_v_idx = double(dr.det_v{fi});
            clu_ids   = dr.clu_ids{fi};
            r_disp_d  = dr.r_disp;
            v_disp_d  = dr.v_disp;
            if isempty(clu_ids), n_clu = 0;
            else, n_clu = max(0, double(max(clu_ids))); end
        else
            % 实时计算（较慢）
            rd_slice = ud.mf_rd.RD_Sum_All(:, :, k);
            pwr      = abs(rd_slice(r_mask, v_mask)).^2;
            cfar_p.guard_r = ud.h_cfar_gr.Value;
            cfar_p.guard_d = ud.h_cfar_gd.Value;
            cfar_p.ref_r   = ud.h_cfar_rr.Value;
            cfar_p.ref_d   = ud.h_cfar_rd.Value;
            cfar_p.pfa     = ud.h_cfar_pfa.Value;
            db_eps = ud.h_db_eps.Value;
            db_min = ud.h_db_min.Value;
            det_mask = cfar_2d(pwr, cfar_p);
            [det_r_idx, det_v_idx] = find(det_mask);
            n_det = numel(det_r_idx);
            if n_det >= db_min
                [clu_ids, n_clu] = dbscan_cluster([det_r_idx, det_v_idx], db_eps, db_min);
                n_clu = max(0, double(n_clu));
            else
                clu_ids = zeros(n_det,1,'int32');  n_clu = 0;
            end
            r_disp_d = r_disp;  v_disp_d = v_disp;
        end

        cla(ax);
        set(ax, 'Color','k', 'XColor','w', 'YColor','w');
        ax.YDir = 'normal';
        xlim(ax, [v_min v_max]);  ylim(ax, [r_min r_max]);
        grid(ax,'on'); box(ax,'on');
        hold(ax,'on');
        if numel(det_r_idx) > 0
            plot(ax, v_disp_d(det_v_idx), r_disp_d(det_r_idx), ...
                '+', 'Color',[0.45 0.45 0.45], 'MarkerSize',4);
        end
        cmap_c = lines(max(n_clu,1));
        for ci = 1:n_clu
            idx_ci = find(clu_ids==ci);
            ri = det_r_idx(idx_ci);  vi = det_v_idx(idx_ci);
            ri_m = min(max(round(mean(ri)),1),numel(r_disp_d));
            vi_m = min(max(round(mean(vi)),1),numel(v_disp_d));
            plot(ax, v_disp_d(vi), r_disp_d(ri), '.','Color',cmap_c(ci,:),'MarkerSize',10);
            plot(ax, v_disp_d(vi_m), r_disp_d(ri_m), 'o','Color',cmap_c(ci,:),'MarkerSize',14,'LineWidth',2);
            text(ax, v_disp_d(vi_m), r_disp_d(ri_m), sprintf(' #%d',ci), ...
                'Color',cmap_c(ci,:),'FontSize',10,'FontWeight','bold');
        end
        hold(ax,'off');
        xlabel(ax,'径向速度 (m/s)','Color','w'); ylabel(ax,'斜距 (m)','Color','w');
        title(ax, sprintf('[CFAR+聚类] 帧 %d/%d | 检测%d点 %d簇', ...
            k, ud.n_frames, numel(det_r_idx), n_clu), 'Interpreter','none','Color','w');
        % 注意：不能 set(ax.Parent,'Color','k')，uipanel 无 Color 属性会报错

    % ----------------------------------------------------------------
    case '测角'
        if ~isempty(ud.ang_result) && k <= numel(ud.ang_result.r_m)
            ar  = ud.ang_result;
            r_m = double(ar.r_m{k});  v_m = double(ar.v_m{k});
            az_m= double(ar.az_m{k}); el_m= double(ar.el_m{k});
            sz_m= double(ar.sz_m{k});
            v_range = ar.v_range;
        else
            % 实时计算（需要三通道）
            sv = who(ud.mf_rd);
            if ~all(ismember({'RD_Sum_All','RD_Az_All','RD_El_All'}, sv))
                cla(ax);
                text(0.5,0.5,'未找到三通道数据（需加载带测角的 mat）', ...
                    'Parent',ax,'Units','normalized','HorizontalAlignment','center');
                return;
            end
            rd0 = ud.mf_rd.RD_Sum_All(:,:,k);
            rd1 = ud.mf_rd.RD_Az_All(:,:,k);
            rd2 = ud.mf_rd.RD_El_All(:,:,k);
            s = rd0(r_mask,v_mask);  a = rd1(r_mask,v_mask);  e = rd2(r_mask,v_mask);
            pwr = abs(s).^2;
            cfar_p.guard_r=ud.h_cfar_gr.Value; cfar_p.guard_d=ud.h_cfar_gd.Value;
            cfar_p.ref_r=ud.h_cfar_rr.Value;   cfar_p.ref_d=ud.h_cfar_rd.Value;
            cfar_p.pfa=ud.h_cfar_pfa.Value;
            db_eps=ud.h_db_eps.Value; db_min=ud.h_db_min.Value;
            det_mask=cfar_2d(pwr,cfar_p);
            [det_r_idx,det_v_idx]=find(det_mask); n_det=numel(det_r_idx);
            if n_det >= db_min
                [clu_ids, n_clu] = dbscan_cluster([det_r_idx, det_v_idx], db_eps, db_min);
            else
                clu_ids = zeros(n_det,1,'int32');  n_clu = 0;
            end
            az_ratio = real(a./(s+eps));  el_ratio = real(e./(s+eps));
            k_mono   = 1.0;
            ang_r_lo = ud.h_ang_r_lo.Value;  ang_r_hi = ud.h_ang_r_hi.Value;
            ang_v_lo = ud.h_ang_v_lo.Value;  ang_v_hi = ud.h_ang_v_hi.Value;
            [r_m, v_m, az_m, el_m, sz_m] = mono_angle(r_disp, v_disp, ...
                det_r_idx, det_v_idx, clu_ids, n_clu, pwr, az_ratio, el_ratio, ...
                k_mono, [ang_r_lo ang_r_hi], [ang_v_lo ang_v_hi], clim_lo);
            v_range = [v_min v_max];
        end

        cla(ax);
        set(ax,'Color',[1 1 1],'XColor','k','YColor','k');
        if numel(r_m) > 0
            yyaxis(ax,'left');
            scatter(ax, az_m(:), r_m(:), sz_m(:), v_m(:), 'filled');
            clim(ax, v_range); colormap(ax, hsv(64));
            ylabel(ax,'斜距 (m)'); xlabel(ax,'方位比值');
            title(ax, sprintf('[测角] 帧%d/%d | 目标%d个', k, ud.n_frames, numel(r_m)), ...
                'Interpreter','none');
        else
            text(0.5,0.5,'无检测目标','Parent',ax,'Units','normalized',...
                'HorizontalAlignment','center','FontSize',14);
        end
        colorbar(ax);
end

drawnow;
end
