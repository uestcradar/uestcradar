function x = read_cs16_channel_file(filePath, numChannels, channelPos)
% READ_CS16_CHANNEL_FILE 按 sample-interleaved CS16 格式读取指定逻辑通道的回波
%   x = read_cs16_channel_file(filePath, numChannels, channelPos)
%
%   数据中的 int16 顺序为：
%     t0: ch1.I, ch1.Q, ch2.I, ch2.Q, ..., chN.I, chN.Q
%     t1: ch1.I, ch1.Q, ch2.I, ch2.Q, ..., chN.I, chN.Q

    fid = fopen(filePath, 'rb');
    if fid == -1
        error('无法打开文件: %s', filePath);
    end

    raw = fread(fid, inf, 'int16=>single', 0, 'ieee-le');
    fclose(fid);

    intsPerTimeSample = 2 * numChannels;
    if mod(numel(raw), intsPerTimeSample) ~= 0
        error('CS16 文件 int16 数量 %d 不能按 %d 通道解交织: %s', ...
            numel(raw), numChannels, filePath);
    end

    rawMat = reshape(raw, intsPerTimeSample, []);
    iRow = (channelPos - 1) * 2 + 1;
    qRow = iRow + 1;
    x = complex(rawMat(iRow, :), rawMat(qRow, :)).';
end
