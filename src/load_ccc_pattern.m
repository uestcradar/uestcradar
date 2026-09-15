function pat = load_ccc_pattern(filepath)
%LOAD_CCC_PATTERN 解析 .ccc 远场方向图文件。
%
% 输入：
%   filepath - .ccc 文件完整路径
% 输出：
%   pat - 结构体，字段：
%     header   - 第1行原始头信息
%     freqs    - 1×nFreq 频点 (MHz)
%     beams    - nBeam×2 波位指向 [az, el] (deg)
%     el_axis  - nEl×1 俯仰角轴 (deg，按文件出现顺序)
%     az_axis  - nAz×1 方位角轴 (deg，按文件出现顺序)
%     data     - nEl×nAz×nFreq×nBeam 复方向图（幅度 dB + 相位° → 复幅值）
%
% 文件格式（逗号分隔 ASCII）：
%   第1行 头信息，含波位指向 ";az&el;az&el;..."
%   第2行 列标题 elevation,azimuth, 然后每频点两列(幅度,相位)标签，每波位重复
%   第3行起 数据：俯仰角,方位角,[幅度dB,相位°]×nFreq×nBeam

fid = fopen(filepath, 'r');
if fid < 0
    error('load_ccc_pattern:OpenFailed', '无法打开方向图文件: %s', filepath);
end
cleanup = onCleanup(@() fclose(fid));

% ---- 第1行：头信息，提取波位指向 ----
line1 = fgetl(fid);
if ~ischar(line1)
    error('load_ccc_pattern:EmptyFile', '方向图文件为空: %s', filepath);
end
pat.header = line1;

beam_pairs = regexp(line1, '([-+]?\d+(?:\.\d+)?)&([-+]?\d+(?:\.\d+)?)', 'tokens');
if isempty(beam_pairs)
    error('load_ccc_pattern:NoBeams', '头行未解析到波位指向(az&el): %s', filepath);
end
nBeam = numel(beam_pairs);
beams = zeros(nBeam, 2);
for i = 1:nBeam
    beams(i, 1) = str2double(beam_pairs{i}{1});
    beams(i, 2) = str2double(beam_pairs{i}{2});
end
pat.beams = beams;

% ---- 第2行：列标题，提取频点（幅度/相位标签成对出现）----
line2 = fgetl(fid);
cols2 = strsplit(strtrim(line2), ',');
num2 = cellfun(@(s) str2double(strtrim(s)), cols2);
freq_labels = num2(3:end);
freq_labels = freq_labels(~isnan(freq_labels));
freqs = unique(freq_labels, 'stable');
nFreq = numel(freqs);
if numel(freq_labels) ~= 2 * nFreq * nBeam
    error('load_ccc_pattern:BadFreq', '频率列数不匹配: 期望 %d，实际 %d', 2 * nFreq * nBeam, numel(freq_labels));
end
pat.freqs = freqs(:).';

% ---- 数据行 ----
data_cell = textscan(fid, '%f', 'Delimiter', ',');
v = data_cell{1};
n_data_cols = 2 + 2 * nFreq * nBeam;
nrows = numel(v) / n_data_cols;
if abs(nrows - round(nrows)) > 1e-6
    error('load_ccc_pattern:BadData', '数据值个数 %d 不能被列数 %d 整除', numel(v), n_data_cols);
end
nrows = round(nrows);
M = reshape(v, n_data_cols, nrows).';  % [nrows, n_data_cols]

el_col = M(:, 1);
az_col = M(:, 2);
data_cols = M(:, 3:end);  % [nrows, 2*nFreq*nBeam]

[pat.el_axis, ~, el_idx] = unique(el_col, 'stable');
[pat.az_axis, ~, az_idx] = unique(az_col, 'stable');
nEl = numel(pat.el_axis);
nAz = numel(pat.az_axis);

% 复方向图（幅度 dB → 幅值，相位° → 复相位）
pat.data = zeros(nEl, nAz, nFreq, nBeam);
lin_pos = sub2ind([nEl, nAz], el_idx, az_idx);
for b = 1:nBeam
    for f = 1:nFreq
        amp_dB  = data_cols(:, (b - 1) * 2 * nFreq + (f - 1) * 2 + 1);
        phs_deg = data_cols(:, (b - 1) * 2 * nFreq + (f - 1) * 2 + 2);
        cpx = 10.^(amp_dB / 20) .* exp(1j * phs_deg * pi / 180);
        slab = zeros(nEl, nAz);
        slab(lin_pos) = cpx;
        pat.data(:, :, f, b) = slab;
    end
end
end
