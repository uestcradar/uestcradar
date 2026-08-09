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

function testReadsOnlyRequestedRows(testCase)
    path = fullfile(testCase.TestData.testDir, 'preview.mat');
    createDataMat(path);

    preview = cyhd_internal.read_ch0_mat_preview(path, 2);

    verifyEqual(testCase, preview.loaded_pulses, uint64(2));
    verifyEqual(testCase, preview.available_pulses, uint64(5));
    verifyEqual(testCase, preview.pri_samples, uint32(8));
    verifyEqual(testCase, size(preview.ch0), [2, 8]);
    expected = complex(single(reshape(1:40, 5, 8)), ...
        single(reshape(101:140, 5, 8)));
    verifyEqual(testCase, preview.ch0, expected(1:2, :));
end

function testRequestIsClampedToAvailableRows(testCase)
    path = fullfile(testCase.TestData.testDir, 'clamped.mat');
    createDataMat(path);

    preview = cyhd_internal.read_ch0_mat_preview(path, 99);

    verifyEqual(testCase, preview.requested_pulses, uint64(99));
    verifyEqual(testCase, preview.loaded_pulses, uint64(5));
    verifyEqual(testCase, size(preview.ch0), [5, 8]);
end

function createDataMat(path)
    data = struct();
    data.sample_rate = 30.72e6;
    data.pri_samples = uint32(8);
    data.ch0 = complex(single(reshape(1:40, 5, 8)), ...
        single(reshape(101:140, 5, 8)));
    data.ch1 = complex(zeros(5, 8, 'single'));
    data.ch2 = complex(zeros(5, 8, 'single'));
    data.beam = struct('timestamp', uint64((1:5).'));
    save(path, 'data', '-v7.3');
end

function removeTestDir(path)
    if isfolder(path)
        rmdir(path, 's');
    end
end
