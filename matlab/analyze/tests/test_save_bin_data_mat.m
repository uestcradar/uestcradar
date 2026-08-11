function tests = test_save_bin_data_mat
%TEST_SAVE_BIN_DATA_MAT Synthetic tests for BIN-to-MAT conversion.
    tests = functiontests(localfunctions);
end

function setupOnce(testCase)
    analyzeDir = fileparts(fileparts(mfilename('fullpath')));
    addpath(analyzeDir);
    testCase.addTeardown(@() rmpath(analyzeDir));
    testDir = tempname;
    mkdir(testDir);
    testCase.TestData.testDir = testDir;
    testCase.addTeardown(@() removeTestDir(testDir));
end

function testChannelsRemainContinuousTimeVectors(testCase)
    first = makeDataFrame(0, 1, 7, 3, 1000, 11, 21);
    second = makeDataFrame(4096, 1, 8, 4, 5096, 12, 22);
    binPath = fullfile(testCase.TestData.testDir, 'matrix.bin');
    writeBytes(binPath, [first; second]);

    info = cyhd_internal.scan_bin_record_info(binPath, 'LogFcn', @(~) []);
    summary = cyhd_internal.save_bin_data_mat(binPath, info, ...
        'OutputDir', testCase.TestData.testDir, 'Overwrite', true);

    verifyEqual(testCase, summary.data_file_count, uint64(1));
    verifyEqual(testCase, summary.saved_frame_count, uint64(2));
    verifyEqual(testCase, summary.saved_sample_count, uint64(8192));

    loaded = load(summary.files.path{1}, 'data');
    verifyEqual(testCase, size(loaded.data.ch0), [8192, 1]);
    verifyEqual(testCase, class(loaded.data.ch0), 'single');
    verifyEqual(testCase, loaded.data.sample_count, uint64(8192));
    verifyEqual(testCase, loaded.data.samples_per_frame, uint32(4096));
    verifyEqual(testCase, loaded.data.channel_layout, 'continuous_time_samples');
    verifyFalse(testCase, isfield(loaded.data, 'pri_samples'));
    verifyEqual(testCase, loaded.data.sample_rate, 30.72e6);

    sampleIndex = single((0:8191).');
    expectedCh0 = complex(sampleIndex, -sampleIndex);
    expectedCh1 = complex(10000 + sampleIndex, -10000 + sampleIndex);
    expectedCh2 = complex(-20000 + sampleIndex, 20000 - sampleIndex);
    verifyEqual(testCase, loaded.data.ch0, expectedCh0);
    verifyEqual(testCase, loaded.data.ch1, expectedCh1);
    verifyEqual(testCase, loaded.data.ch2, expectedCh2);

    verifyEqual(testCase, loaded.data.beam.timestamp, uint64([1000; 5096]));
    verifyEqual(testCase, loaded.data.beam.sweep_count, uint8([1; 1]));
    verifyEqual(testCase, loaded.data.beam.pulse_in_beam, uint16([7; 8]));
    verifyEqual(testCase, loaded.data.beam.current_beam_idx, uint16([3; 4]));
    verifyEqual(testCase, loaded.data.beam.az_code, uint16([11; 12]));
    verifyEqual(testCase, loaded.data.beam.el_code, uint16([21; 22]));
end

function testBadFrameProducesSeparateSegmentFiles(testCase)
    first = makeDataFrame(0, 0, 0, 0, 1000, 0, 0);
    bad = makeDataFrame(4096, 0, 1, 0, 5096, 0, 0);
    bad(hex2dec('06E00') + 1) = uint8(0);
    third = makeDataFrame(8192, 0, 2, 0, 9192, 0, 0);
    binPath = fullfile(testCase.TestData.testDir, 'split.bin');
    writeBytes(binPath, [first; bad; third]);

    info = cyhd_internal.scan_bin_record_info(binPath, 'LogFcn', @(~) []);
    summary = cyhd_internal.save_bin_data_mat(binPath, info, ...
        'OutputDir', testCase.TestData.testDir, 'Overwrite', true);

    verifyEqual(testCase, info.summary.bad_frame_count, uint64(1));
    verifyEqual(testCase, info.summary.continuous_segment_count, uint64(2));
    verifyEqual(testCase, summary.data_file_count, uint64(2));
    verifyEqual(testCase, summary.saved_frame_count, uint64(2));
    verifyEqual(testCase, summary.saved_sample_count, uint64(8192));
    verifyTrue(testCase, all(cellfun(@isfile, summary.files.path)));
end

function testLegacyRecordInfoIsRejected(testCase)
    frame = makeDataFrame(0, 0, 0, 0, 1000, 0, 0);
    binPath = fullfile(testCase.TestData.testDir, 'legacy_record_info.bin');
    writeBytes(binPath, frame);
    info = cyhd_internal.scan_bin_record_info(binPath, 'LogFcn', @(~) []);
    info.protocol.beam_offset = hex2dec('06E00');
    info.protocol.data_header_offset = hex2dec('06E20');
    info.protocol.data_header_bytes = 96;
    info.protocol.data_header_pattern = info.protocol.data_header_pattern(1:96);

    verifyError(testCase, @() cyhd_internal.save_bin_data_mat( ...
        binPath, info, 'OutputDir', testCase.TestData.testDir), ...
        'CYHD:UnsupportedProtocol');
end

function frame = makeDataFrame(globalStart, sweep, pulse, beam, timestamp, azCode, elCode)
    frame = zeros(160000, 1, 'uint8');
    frame(1:128) = uint8(hex2dec('5A'));

    scanMeta0 = bitor(bitshift(uint32(sweep), 27), ...
        bitor(bitshift(uint32(pulse), 9), uint32(beam)));
    words = [uint32(hex2dec('4245414D')); scanMeta0; ...
        uint32(bitand(uint64(timestamp), uint64(hex2dec('FFFFFFFF')))); ...
        uint32(bitshift(uint64(timestamp), -32)); ...
        uint32(azCode); uint32(elCode); uint32(0); uint32(0)];
    frame(hex2dec('06DE0') + (1:32)) = encodeUint32Words(words);
    frame(hex2dec('06E00') + (1:128)) = repmat( ...
        uint8([hex2dec('FE'); hex2dec('60'); hex2dec('60'); hex2dec('60')]), 32, 1);

    sampleIndex = int16(globalStart + (0:4095));
    payloadWords = zeros(16, 4096, 'int16');
    payloadWords(1, :) = sampleIndex;
    payloadWords(2, :) = -sampleIndex;
    payloadWords(3, :) = int16(10000 + double(sampleIndex));
    payloadWords(4, :) = int16(-10000 + double(sampleIndex));
    payloadWords(5, :) = int16(-20000 + double(sampleIndex));
    payloadWords(6, :) = int16(20000 - double(sampleIndex));
    payloadWords(9:16, :) = int16(1234);
    frame(hex2dec('06E80') + (1:131072)) = encodeInt16Words(payloadWords(:));
    frame(hex2dec('26E80') + (1:512)) = repmat(uint8([7; 0; 0; 0]), 128, 1);
    frame(hex2dec('27080') + (1:128)) = uint8(hex2dec('A5'));
end

function bytes = encodeUint32Words(words)
    bytes = zeros(4 * numel(words), 1, 'uint8');
    words = words(:);
    bytes(1:4:end) = uint8(bitand(words, uint32(255)));
    bytes(2:4:end) = uint8(bitand(bitshift(words, -8), uint32(255)));
    bytes(3:4:end) = uint8(bitand(bitshift(words, -16), uint32(255)));
    bytes(4:4:end) = uint8(bitshift(words, -24));
end

function bytes = encodeInt16Words(words)
    unsignedWords = typecast(words(:), 'uint16');
    bytes = zeros(2 * numel(unsignedWords), 1, 'uint8');
    bytes(1:2:end) = uint8(bitand(unsignedWords, uint16(255)));
    bytes(2:2:end) = uint8(bitshift(unsignedWords, -8));
end

function writeBytes(path, bytes)
    fid = fopen(path, 'wb');
    if fid == -1
        error('Test:OpenFailed', 'Unable to create %s', path);
    end
    cleanup = onCleanup(@() fclose(fid));
    fwrite(fid, bytes, 'uint8');
end

function removeTestDir(path)
    if isfolder(path)
        rmdir(path, 's');
    end
end
