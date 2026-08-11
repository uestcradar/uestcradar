function preview = read_ch0_mat_preview(matPath, requestedSamples)
%READ_CH0_MAT_PREVIEW Read the first N continuous CH0 samples from a MAT.
%   Uses the HDF5 layout of MATLAB v7.3 files, so CH1/CH2 and the remaining
%   CH0 samples are not loaded into memory.

    arguments
        matPath {mustBeTextScalar}
        requestedSamples (1, 1) double {mustBeFinite, mustBePositive, mustBeInteger}
    end

    matPath = char(matPath);
    if ~isfile(matPath)
        error('CYHD:FileNotFound', 'MAT 文件不存在: %s', matPath);
    end

    try
        channelInfo = h5info(matPath, '/data/ch0');
        sampleRate = double(h5read(matPath, '/data/sample_rate'));
        declaredSampleCount = double(h5read(matPath, '/data/sample_count'));
    catch ME
        error('CYHD:InvalidDataMat', ...
            'MAT 文件不符合连续通道格式，缺少 data.ch0/sample_rate/sample_count: %s', ...
            ME.message);
    end

    channelSize = double(channelInfo.Dataspace.Size);
    if numel(channelSize) ~= 2 || any(channelSize < 1) || all(channelSize ~= 1)
        error('CYHD:InvalidDataMat', 'data.ch0 必须是非空的一维行或列向量。');
    end
    availableSamples = prod(channelSize);
    if ~isscalar(declaredSampleCount) || declaredSampleCount ~= availableSamples
        error('CYHD:InvalidDataMat', ...
            'data.sample_count=%g 与 data.ch0点数=%g不一致。', ...
            declaredSampleCount, availableSamples);
    end
    if ~isscalar(sampleRate) || ~isfinite(sampleRate) || sampleRate <= 0
        error('CYHD:InvalidDataMat', 'data.sample_rate 不是有效采样率。');
    end

    sampleCount = min(requestedSamples, availableSamples);
    if channelSize(1) == 1
        count = [1, sampleCount];
    else
        count = [sampleCount, 1];
    end
    raw = h5read(matPath, '/data/ch0', [1, 1], count);
    if isstruct(raw) && isfield(raw, 'real') && isfield(raw, 'imag')
        ch0 = complex(single(raw.real), single(raw.imag));
    elseif isnumeric(raw)
        ch0 = single(raw);
    else
        error('CYHD:InvalidDataMat', 'data.ch0 的HDF5数据类型无法解析。');
    end

    preview = struct();
    preview.file_path = matPath;
    preview.sample_rate = sampleRate;
    preview.available_samples = uint64(availableSamples);
    preview.requested_samples = uint64(requestedSamples);
    preview.loaded_samples = uint64(sampleCount);
    preview.ch0 = ch0(:);
end
