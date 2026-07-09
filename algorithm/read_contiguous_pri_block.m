function rxMat = read_contiguous_pri_block(filePath, priLen, startPri, numPri, ...
    numRxChannels, selectedChannelPos, rangeZeroBin)
% READ_CONTIGUOUS_PRI_BLOCK 从回波文件中读取一段连续 PRI，并排列成 [pulse, fast_time]。
%   rxMat = read_contiguous_pri_block(filePath, priLen, startPri, numPri, numRxChannels, selectedChannelPos, rangeZeroBin)
%   使用 single 类型可以降低内存占用，适合动画和 GUI 动态滑动处理。

    if nargin < 7
        rangeZeroBin = 0;
    end

    bytesPerSample = numRxChannels * 2 * 2;
    bytesPerPri = priLen * bytesPerSample;
    byteOffset = (startPri - 1) * bytesPerPri + rangeZeroBin * bytesPerSample;
    numInt16ToRead = numPri * priLen * numRxChannels * 2;

    fid = fopen(filePath, 'rb');
    if fid == -1
        error('无法打开回波文件: %s', filePath);
    end

    status = fseek(fid, byteOffset, 'bof');
    if status ~= 0
        fclose(fid);
        error('无法跳转到起始 PRI %d。', startPri);
    end

    raw = fread(fid, numInt16ToRead, 'int16=>single', 0, 'ieee-le');
    fclose(fid);

    if numel(raw) ~= numInt16ToRead
        error('未读到完整的 PRI 数据。期望 int16 数 %d，实际 %d。', ...
            numInt16ToRead, numel(raw));
    end

    rawMat = reshape(raw, 2 * numRxChannels, []);
    iRow = (selectedChannelPos - 1) * 2 + 1;
    qRow = iRow + 1;
    rx = complex(rawMat(iRow, :), rawMat(qRow, :));
    rxMat = reshape(rx, priLen, numPri).';
end
