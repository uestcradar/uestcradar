function [clu_ids, n_clu] = dbscan_cluster(pts, eps_val, min_pts)
%DBSCAN_CLUSTER 使用 DBSCAN 对检测点进行聚类。
%
% 输入：
%   pts      - 检测点坐标，大小为 N x 2
%   eps_val  - DBSCAN 邻域半径
%   min_pts  - 成簇所需的最小点数
% 输出：
%   clu_ids  - 每个点的聚类编号
%   n_clu    - 聚类数量
% 作用：
%   对 CFAR 输出点进行空间聚类，便于后续测角与目标提取。
%   单独保留这个模块的原因是：聚类策略和检测策略并不绑定，
%   后续如果要改成连通域聚类、K-means 或手工门限合并，只需要替换这里，
%   主流程里“检测后按簇组织目标”的框架可以保持不变。

if isempty(pts)
    clu_ids = int32([]);
    n_clu = 0;
    return;
end

try
    clu_ids = int32(dbscan(double(pts), eps_val, min_pts));
catch
    clu_ids = fallback_dbscan(pts, eps_val, min_pts);
end

n_clu = max(0, double(max(clu_ids)));
end

function clu_ids = fallback_dbscan(X, eps_r, min_pts)
%FALLBACK_DBSCAN 当系统没有 dbscan 函数时的简化实现。
%
% 输入：
%   X       - 点坐标
%   eps_r   - 邻域半径
%   min_pts - 成簇最小点数
% 输出：
%   clu_ids - 聚类编号
% 作用：
%   提供一个不依赖额外工具箱的 DBSCAN 备选实现。
%   单独写成子函数的原因是：优先使用 MATLAB 自带 dbscan，
%   但在缺少相关工具箱时仍希望主流程可运行，所以这里提供兼容回退。

n = size(X, 1);
clu_ids = zeros(n, 1, 'int32');
visited = false(n, 1);
cid = 0;

for i = 1:n
    if visited(i)
        continue;
    end
    visited(i) = true;
    D = sqrt(sum((X - X(i, :)).^2, 2));
    nb = find(D <= eps_r);
    if numel(nb) < min_pts
        clu_ids(i) = -1;
    else
        cid = cid + 1;
        clu_ids(i) = cid;
        queue = nb(nb ~= i);
        while ~isempty(queue)
            j = queue(1);
            queue(1) = [];
            if ~visited(j)
                visited(j) = true;
                Dj = sqrt(sum((X - X(j, :)).^2, 2));
                nbj = find(Dj <= eps_r);
                if numel(nbj) >= min_pts
                    queue = [queue; nbj(~ismember(nbj, queue))]; %#ok<AGROW>
                end
            end
            if clu_ids(j) == 0
                clu_ids(j) = cid;
            end
        end
    end
end
end
