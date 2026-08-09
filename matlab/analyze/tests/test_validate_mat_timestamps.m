function tests = test_validate_mat_timestamps
%TEST_VALIDATE_MAT_TIMESTAMPS Tests for full frame timestamp validation.
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

function testContinuousTimestampsPassAcrossChunks(testCase)
    path = fullfile(testCase.TestData.testDir, 'continuous.mat');
    createTimestampMat(path, uint64([100; 100; 4196; 4196; 8292]));

    report = cyhd_internal.validate_mat_timestamps(path, 'ChunkEntries', 2);

    verifyEqual(testCase, report.status, 'PASS');
    verifyEqual(testCase, report.timestamp_count, uint64(5));
    verifyEqual(testCase, report.transition_count, uint64(4));
    verifyEqual(testCase, report.discontinuity_count, uint64(0));
    verifyEqual(testCase, report.unchanged_transition_count, uint64(2));
    verifyEqual(testCase, report.advanced_4096_transition_count, uint64(2));
    verifyEqual(testCase, height(report.issues), 0);
end

function testGapAndBackwardAreReported(testCase)
    path = fullfile(testCase.TestData.testDir, 'broken.mat');
    createTimestampMat(path, uint64([100; 4196; 12388; 12388; 12000]));

    report = cyhd_internal.validate_mat_timestamps(path, 'ChunkEntries', 2);

    verifyEqual(testCase, report.status, 'FAIL');
    verifyEqual(testCase, report.discontinuity_count, uint64(2));
    verifyEqual(testCase, report.forward_gap_count, uint64(1));
    verifyEqual(testCase, report.backward_count, uint64(1));
    verifyEqual(testCase, report.unchanged_transition_count, uint64(1));
    verifyEqual(testCase, report.advanced_4096_transition_count, uint64(1));
    verifyEqual(testCase, report.inferred_missing_frame_count, uint64(1));
    verifyEqual(testCase, report.forward_missing_ticks, uint64(4096));
    verifyEqual(testCase, report.issues.current_frame_index, uint64([3; 5]));
    verifyEqual(testCase, report.issues.signed_delta_ticks, int64([8192; -388]));
end

function createTimestampMat(path, timestamps)
    data = struct();
    data.sample_rate = 30.72e6;
    data.pri_samples = uint32(8);
    data.ch0 = complex(zeros(numel(timestamps), 8, 'single'));
    data.ch1 = data.ch0;
    data.ch2 = data.ch0;
    data.beam = struct('timestamp', timestamps(:));
    save(path, 'data', '-v7.3');
end

function removeTestDir(path)
    if isfolder(path)
        rmdir(path, 's');
    end
end
