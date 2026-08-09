function preview = read_ch0_mat_preview(matPath, requestedPulses)
%READ_CH0_MAT_PREVIEW Read the first N CH0 pulses from a converted MAT.
%   Uses the HDF5 layout of MATLAB v7.3 files, so CH1/CH2 and the remaining
%   CH0 pulses are not loaded into memory.

    arguments
        matPath {mustBeTextScalar}
        requestedPulses (1, 1) double {mustBeFinite, mustBePositive, mustBeInteger}
    end

    matPath = char(matPath);
    if ~isfile(matPath)
        error('CYHD:FileNotFound', 'MAT 文件不存在: %s', matPath);
    end

    try
        channelInfo = h5info(matPath, '/data/ch0');
        sampleRate = double(h5read(matPath, '/data/sample_rate'));
        priSamples = double(h5read(matPath, '/data/pri_samples'));
    catch ME
        error('CYHD:InvalidDataMat', ...
            'MAT 文件不符合解析数据格式，缺少 data.ch0/sample_rate/pri_samples: %s', ...
            ME.message);
    end

    channelSize = double(channelInfo.Dataspace.Size);
    if numel(channelSize) ~= 2 || any(channelSize < 1)
        error('CYHD:InvalidDataMat', 'data.ch0 必须是二维非空矩阵。');
    end
    availablePulses = channelSize(1);
    fastTimeSamples = channelSize(2);
    if priSamples ~= fastTimeSamples
        error('CYHD:InvalidDataMat', ...
            'data.pri_samples=%d 与 data.ch0 快时间维度=%d 不一致。', ...
            priSamples, fastTimeSamples);
    end
    if ~isscalar(sampleRate) || ~isfinite(sampleRate) || sampleRate <= 0
        error('CYHD:InvalidDataMat', 'data.sample_rate 不是有效采样率。');
    end

    pulseCount = min(requestedPulses, availablePulses);
    raw = h5read(matPath, '/data/ch0', [1, 1], [pulseCount, fastTimeSamples]);
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
    preview.pri_samples = uint32(priSamples);
    preview.available_pulses = uint64(availablePulses);
    preview.requested_pulses = uint64(requestedPulses);
    preview.loaded_pulses = uint64(pulseCount);
    preview.ch0 = ch0;
end
