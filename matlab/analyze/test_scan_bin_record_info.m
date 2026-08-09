function tests = test_scan_bin_record_info
%TEST_SCAN_BIN_RECORD_INFO Synthetic protocol tests for the BIN scanner.
    tests = functiontests(localfunctions);
end

function setupOnce(testCase)
    testDir = tempname;
    mkdir(testDir);
    testCase.TestData.testDir = testDir;
    testCase.addTeardown(@() removeTestDir(testDir));
end

function testValidFramesPass(testCase)
    frames = {
        makeFrame(0, 0, 3, 100, 10, 20), ...
        makeFrame(0, 1, 3, 101, 10, 20), ...
        makeFrame(0, 2, 3, 102, 10, 20), ...
        makeFrame(0, 0, 4, 103, 11, 20)};
    path = fullfile(testCase.TestData.testDir, 'valid.bin');
    writeBytes(path, vertcat(frames{:}));

    info = quietScan(path);

    verifyEqual(testCase, info.summary.status, 'PASS');
    verifyEqual(testCase, info.summary.valid_frame_count, uint64(4));
    verifyEqual(testCase, info.summary.continuous_segment_count, uint64(1));
    verifyEqual(testCase, info.structural.metadata_byte_order, 'little-endian');
    verifyEqual(testCase, height(info.beam_summary), 2);
    verifyEqual(testCase, height(info.issues), 0);
    verifyFalse(testCase, info.structural.crc_region_checked);
end

function testSemanticJumpWarns(testCase)
    frames = {
        makeFrame(1, 0, 7, 200, 20, 30), ...
        makeFrame(1, 3, 7, 201, 20, 30)};
    path = fullfile(testCase.TestData.testDir, 'semantic_jump.bin');
    writeBytes(path, vertcat(frames{:}));

    info = quietScan(path);

    verifyEqual(testCase, info.summary.status, 'WARN');
    verifyEqual(testCase, info.semantic.pulse_in_beam.jump_forward_count, uint64(1));
    verifyTrue(testCase, any(strcmp(info.issues.type, 'COUNTER_JUMP_FORWARD')));
end

function testStructuralDamageAndTruncatedTail(testCase)
    leading = uint8((1:7).');
    good1 = makeFrame(0, 0, 1, 10, 1, 2);
    badHeader = makeFrame(0, 1, 1, 11, 1, 2);
    badHeader(hex2dec('06E20') + 1) = uint8(0);
    good2 = makeFrame(0, 2, 1, 12, 1, 2);
    partial = makeFrame(0, 3, 1, 13, 1, 2);
    partial = partial(1:100);
    path = fullfile(testCase.TestData.testDir, 'damaged.bin');
    writeBytes(path, [leading; good1; badHeader; good2; partial]);

    info = quietScan(path);

    verifyEqual(testCase, info.summary.status, 'FAIL');
    verifyEqual(testCase, info.summary.valid_frame_count, uint64(2));
    verifyEqual(testCase, info.structural.leading_bytes, uint64(7));
    verifyEqual(testCase, info.structural.data_header_error_count, uint64(1));
    verifyEqual(testCase, info.structural.truncated_tail_bytes, uint64(100));
    verifyEqual(testCase, info.summary.continuous_segment_count, uint64(2));
end

function testBadTailResynchronizes(testCase)
    badTail = makeFrame(0, 0, 0, 1, 0, 0);
    badTail(hex2dec('27080') + 1) = uint8(0);
    good = makeFrame(0, 1, 0, 2, 0, 0);
    path = fullfile(testCase.TestData.testDir, 'resync.bin');
    writeBytes(path, [badTail; good]);

    info = quietScan(path);

    verifyEqual(testCase, info.summary.status, 'FAIL');
    verifyEqual(testCase, info.structural.tail_error_count, uint64(1));
    verifyEqual(testCase, info.summary.valid_frame_count, uint64(1));
    verifyGreaterThanOrEqual(testCase, info.structural.resync_count, uint64(1));
end

function testEmptyFileFails(testCase)
    path = fullfile(testCase.TestData.testDir, 'empty.bin');
    writeBytes(path, zeros(0, 1, 'uint8'));

    info = quietScan(path);

    verifyEqual(testCase, info.summary.status, 'FAIL');
    verifyTrue(testCase, any(strcmp(info.issues.type, 'EMPTY_FILE')));
end

function testBoundaryFragmentsOnlyWarn(testCase)
    leading = uint8((1:11).');
    good = makeFrame(0, 0, 0, 1, 0, 0);
    partial = makeFrame(0, 1, 0, 2, 0, 0);
    partial = partial(1:100);
    path = fullfile(testCase.TestData.testDir, 'boundary_fragments.bin');
    writeBytes(path, [leading; good; partial]);

    info = quietScan(path);

    verifyEqual(testCase, info.summary.status, 'WARN');
    verifyEqual(testCase, info.summary.structural_issue_count, uint64(0));
    verifyEqual(testCase, info.summary.boundary_issue_count, uint64(2));
    verifyEqual(testCase, info.structural.resync_count, uint64(0));
    verifyEqual(testCase, info.summary.valid_frame_count, uint64(1));
end

function testGapDurationInference(testCase)
    first = makeFrame(0, 0, 0, 1000, 0, 0);
    bad = makeFrame(0, 1, 0, 5096, 0, 0);
    bad(hex2dec('06E20') + 1) = uint8(0);
    next = makeFrame(0, 3, 0, 13288, 0, 0);
    path = fullfile(testCase.TestData.testDir, 'timestamp_gap.bin');
    writeBytes(path, [first; bad; next]);

    info = quietScan(path);

    verifyEqual(testCase, info.summary.bad_frame_count, uint64(1));
    verifyEqual(testCase, info.summary.gap_count, uint64(1));
    verifyEqual(testCase, info.summary.inferred_missing_frame_count, uint64(2));
    verifyEqual(testCase, info.summary.unresolved_gap_count, uint64(0));
    verifyEqual(testCase, info.gaps.inferred_missing_duration_seconds, ...
        2 / 7500, 'AbsTol', 1e-12);
end

function info = quietScan(path)
    info = scan_bin_record_info(path, 'LogFcn', @(~) []);
end

function frame = makeFrame(sweep, pulse, beam, timestamp, azCode, elCode)
    frame = zeros(160000, 1, 'uint8');
    frame(1:128) = uint8(hex2dec('5A'));

    scanMeta0 = bitor(bitshift(uint32(sweep), 27), ...
        bitor(bitshift(uint32(pulse), 9), uint32(beam)));
    words = [uint32(hex2dec('4245414D')); scanMeta0; ...
        uint32(bitand(uint64(timestamp), uint64(hex2dec('FFFFFFFF')))); ...
        uint32(bitshift(uint64(timestamp), -32)); ...
        uint32(azCode); uint32(elCode); uint32(0); uint32(0)];
    metadata = zeros(32, 1, 'uint8');
    for index = 1:8
        metadata((index - 1) * 4 + (1:4)) = uint32ToLittleEndian(words(index));
    end
    frame(hex2dec('06E00') + (1:32)) = metadata;
    frame(hex2dec('06E20') + (1:96)) = repmat(uint8([hex2dec('FE'); hex2dec('60'); hex2dec('60'); hex2dec('60')]), 24, 1);
    % Real captures carry nonzero CRC/status words here.  The scanner must
    % preserve this region without treating it as zero padding.
    frame(hex2dec('26E80') + (1:512)) = repmat(uint8([7; 0; 0; 0]), 128, 1);
    frame(hex2dec('27080') + (1:128)) = uint8(hex2dec('A5'));
end

function bytes = uint32ToLittleEndian(value)
    bytes = uint8([bitand(value, uint32(255)); ...
        bitand(bitshift(value, -8), uint32(255)); ...
        bitand(bitshift(value, -16), uint32(255)); ...
        bitshift(value, -24)]);
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
