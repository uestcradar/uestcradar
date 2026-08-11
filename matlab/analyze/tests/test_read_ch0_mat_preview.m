function tests = test_read_ch0_mat_preview
%TEST_READ_CH0_MAT_PREVIEW Tests for partial CH0 MAT loading.
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

function testReadsOnlyRequestedSamples(testCase)
    path = fullfile(testCase.TestData.testDir, 'preview.mat');
    createDataMat(path);

    preview = cyhd_internal.read_ch0_mat_preview(path, 12);

    verifyEqual(testCase, preview.loaded_samples, uint64(12));
    verifyEqual(testCase, preview.available_samples, uint64(40));
    verifyEqual(testCase, size(preview.ch0), [12, 1]);
    expected = complex(single((1:40).'), single((101:140).'));
    verifyEqual(testCase, preview.ch0, expected(1:12));
end

function testRequestIsClampedToAvailableSamples(testCase)
    path = fullfile(testCase.TestData.testDir, 'clamped.mat');
    createDataMat(path);

    preview = cyhd_internal.read_ch0_mat_preview(path, 99);

    verifyEqual(testCase, preview.requested_samples, uint64(99));
    verifyEqual(testCase, preview.loaded_samples, uint64(40));
    verifyEqual(testCase, size(preview.ch0), [40, 1]);
end

function createDataMat(path)
    data = struct();
    data.sample_rate = 30.72e6;
    data.sample_count = uint64(40);
    data.samples_per_frame = uint32(4096);
    data.channel_layout = 'continuous_time_samples';
    data.ch0 = complex(single((1:40).'), single((101:140).'));
    data.ch1 = complex(zeros(40, 1, 'single'));
    data.ch2 = complex(zeros(40, 1, 'single'));
    data.beam = struct('timestamp', uint64((1:5).'));
    save(path, 'data', '-v7.3');
end

function removeTestDir(path)
    if isfolder(path)
        rmdir(path, 's');
    end
end
