%LOCAL_CHECK_RUN_BATCH_PIPELINE 本地自动化运行入口脚本的临时包装器。
%
% 用途：
%   1. 不改正式入口脚本内容
%   2. 为本地检查临时注入数据目录和开关
%   3. 生成一个可直接 run 的脚本文件，再调用 MATLAB 正常执行

apps_dir = 'Z:\apps';
data_dir = 'Y:\20M_64tao_E2';

src_file = fullfile(apps_dir, 'run_batch_pipeline.m');
txt = fileread(src_file);

txt = regexprep(txt, ...
    'cfg\.data_folders = \{[\s\S]*?\};', ...
    sprintf('cfg.data_folders = {\n    ''%s''\n    };', data_dir));
txt = regexprep(txt, 'cfg\.do_parse = true;', 'cfg.do_parse = false;');
txt = regexprep(txt, 'cfg\.do_plot = true;', 'cfg.do_plot = false;');
txt = regexprep(txt, ...
    'this_dir = fileparts\(mfilename\(''fullpath''\)\);\s*project_root = fileparts\(this_dir\);', ...
    sprintf('this_dir = ''%s'';\nproject_root = fileparts(this_dir);', apps_dir));

generated_file = fullfile(tempdir, 'run_batch_pipeline_local_check_generated.m');
fid = fopen(generated_file, 'w', 'n', 'UTF-8');
if fid == -1
    error('local_check_run_batch_pipeline:OpenFailed', ...
        'Unable to create generated script: %s', generated_file);
end
fwrite(fid, unicode2native(txt, 'UTF-8'), 'uint8');
fclose(fid);

run(generated_file);
