function meta = read_json_file(filePath)
% READ_JSON_FILE 读取并解析 JSON 文件
%   meta = read_json_file(filePath)

    try
        txt = fileread(filePath);
        meta = jsondecode(txt);
    catch ME
        error('无法读取或解析 JSON: %s\n%s', filePath, ME.message);
    end
end
