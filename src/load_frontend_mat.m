function parse_bundle = load_frontend_mat(mat_file, tx_dir_spec, lg)
%LOAD_FRONTEND_MAT 从前端 .mat 构建 parse_bundle。
%
% 拆分文件已存在 → 轻量路径（h5read 元数据，不碰 matfile）。
% 拆分文件不存在 → 完整路径（matfile 读元数据 + 拆分为 flat 文件，仅首次）。

if nargin < 3 || isempty(lg)
    lg = @(msg) fprintf('%s\n', msg);
end

if ~exist(mat_file, 'file')
    error('load_frontend_mat:MissingMat', '前端 mat 文件不存在: %s', mat_file);
end

[src_dir, src_name] = fileparts(mat_file);
base_name = regexprep(src_name, '_data_seg\d+$', '');

channel_names = {'ch0', 'ch1', 'ch2'};
channel_files = cell(1, 3);
all_ready = true;
for ch = 1:3
    channel_files{ch} = fullfile(src_dir, [base_name '_' channel_names{ch} '.mat']);
    if ~exist(channel_files{ch}, 'file'), all_ready = false; end
end

pri_per_frame = 4;

if all_ready
    % ========== 轻量路径：拆分文件已就绪 ==========
    lg('[适配] 拆分文件已就绪，轻量加载...');

    info = whos('-file', channel_files{1});
    ch0_info = info(strcmp({info.name}, 'ch0'));
    sample_count = ch0_info.size(1);

    sample_rate       = double(h5read(mat_file, '/data/sample_rate'));
    samples_per_frame = double(h5read(mat_file, '/data/samples_per_frame'));

    frame_az_code = double(h5read(mat_file, '/data/beam/az_code'));
    frame_el_code = double(h5read(mat_file, '/data/beam/el_code'));
    frame_sweep   = double(h5read(mat_file, '/data/beam/sweep_count'));

else
    % ========== 完整路径：首次拆分 ==========
    lg('[适配] 首次运行，执行轻量拆分（不使用 matfile 全量加载）...');

    for ch = 1:3
        if exist(channel_files{ch}, 'file'), delete(channel_files{ch}); end
    end

    % ---- 用 h5read 读取元数据（只读标量/小数组，不碰大数据）----
    sample_rate       = double(h5read(mat_file, '/data/sample_rate'));
    sample_count      = double(h5read(mat_file, '/data/sample_count'));
    samples_per_frame = double(h5read(mat_file, '/data/samples_per_frame'));

    lg(sprintf('[适配] 采样率=%.1f MHz, 总采样=%.3f M, 帧数=%d', ...
        sample_rate/1e6, sample_count/1e6, sample_count/samples_per_frame));

    frame_az_code = double(h5read(mat_file, '/data/beam/az_code'));
    frame_el_code = double(h5read(mat_file, '/data/beam/el_code'));
    frame_sweep   = double(h5read(mat_file, '/data/beam/sweep_count'));

    % ---- 拆分：直接用 h5read 分段读取 HDF5 数据集，全程不加载全量 ----
    CHUNK_SAMPLES = 1e6;
    h5_chunk = min(CHUNK_SAMPLES, sample_count);  % 单次读取上限

    for ch = 1:3
        ch_name = channel_names{ch};
        out_file = channel_files{ch};
        h5_path = ['/data/' ch_name];             % v7.3 mat 内部的 HDF5 路径

        % 获取实际数据类型，确保写入一致
        h5_info = h5info(mat_file, h5_path);
        actual_samples = h5_info.Dataspace.Size(1);

        dummy = single(0);
        save(out_file, 'dummy', '-v7.3');
        mf_out = matfile(out_file, 'Writable', true);

        n_chunks = ceil(actual_samples / h5_chunk);
        t_start = tic;
        for ci = 1:n_chunks
            s0 = (ci - 1) * h5_chunk + 1;
            s1 = min(ci * h5_chunk, actual_samples);
            count = s1 - s0 + 1;

            % 直接读 HDF5 切片 → 写入独立通道文件
            data_slice = h5read(mat_file, h5_path, [s0 1], [count 1]);
            if isstruct(data_slice)
                % v7.3 complex 数据：h5read 返回 real/imag 分体 struct
                data_slice = complex(data_slice.real, data_slice.imag);
            end
            mf_out.(ch_name)(s0:s1, 1) = single(data_slice);

            if mod(ci, 10) == 0 || ci == n_chunks
                lg(sprintf('[拆分] %s: %.0f%% (%d/%d) %.0fs', ...
                    ch_name, s1/actual_samples*100, ci, n_chunks, toc(t_start)));
            end
        end
        clear mf_out data_slice;
        lg(sprintf('[拆分] %s 完成: %.1f GB, %.0f s', ...
            ch_name, dir(out_file).bytes/1e9, toc(t_start)));
    end
end

% =====================================================================
%  构建 beam_meta（两条路径共用）
% =====================================================================
pri_len  = samples_per_frame / pri_per_frame;
n_frames = sample_count / samples_per_frame;

frame_az_deg = single(frame_az_code * 0.05 - 50.0);
frame_el_deg = single(frame_el_code * 0.05 - 50.0);
frame_valid  = true(n_frames, 1);

beam_meta = struct();
beam_meta.az_deg   = repelem(frame_az_deg, pri_per_frame);
beam_meta.el_deg   = repelem(frame_el_deg, pri_per_frame);
beam_meta.sweep_count = repelem(uint8(frame_sweep), pri_per_frame);
beam_meta.meta_valid  = repelem(frame_valid, pri_per_frame);
beam_meta.az_code  = repelem(uint16(frame_az_code), pri_per_frame);
beam_meta.el_code  = repelem(uint16(frame_el_code), pri_per_frame);

% =====================================================================
%  TX 参考
% =====================================================================
if nargin < 2 || isempty(tx_dir_spec)
    tx_dir_spec = fileparts(mat_file);
end
tx = load_tx_reference(tx_dir_spec, lg);

% =====================================================================
%  组装 parse_bundle
% =====================================================================
rx_param = struct();
rx_param.sample_rate = sample_rate;
rx_param.pri_len     = pri_len;
rx_param.total_pri   = sample_count / pri_len;
rx_param.total_samples = sample_count;
rx_param.pri_per_frame = pri_per_frame;
rx_param.channels    = [0, 1, 2];
rx_param.prf         = sample_rate / pri_len;
rx_param.cpi_files   = {mat_file};

parse_bundle = struct();
parse_bundle.rx_param  = rx_param;
parse_bundle.tx        = tx;
parse_bundle.beam_meta = beam_meta;
parse_bundle.data_dir  = src_dir;
parse_bundle.channel_ids = rx_param.channels;
parse_bundle.channel_var_names = channel_names;
parse_bundle.rx_channel_files = channel_files;
parse_bundle.parse_info_file = mat_file;
parse_bundle.parse_ts = char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'));

lg('[适配] parse_bundle 构建完成');
end

function tx = load_tx_reference(tx_dir, lg)
tx_file = fullfile(tx_dir, 'lfm_tx.bin');
tx_meta_file = fullfile(tx_dir, 'metadata.json');

if ~exist(tx_file, 'file')
    error('load_frontend_mat:MissingTx', 'TX 参考文件不存在: %s', tx_file);
end

fid = fopen(tx_file, 'rb');
tx_raw = fread(fid, inf, 'int16');
fclose(fid);
tx_data = double(tx_raw(1:2:end)) + 1j * double(tx_raw(2:2:end));

tx = struct();
tx.data = single(tx_data(:));

if exist(tx_meta_file, 'file')
    tx.meta = jsondecode(fileread(tx_meta_file));
else
    tx.meta = [];
end

lg(sprintf('[适配] TX 参考加载: %s (%d 采样)', tx_file, numel(tx_data)));
end
