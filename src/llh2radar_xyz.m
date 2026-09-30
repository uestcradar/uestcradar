function xyz = llh2radar_xyz(uav_llh, radar_llh, bearing_deg)
%LLH2RADAR_XYZ 经纬高(椭球) → 雷达相对直角坐标，坐标系与 tracker_3D_EKF 一致。
%
% 输入:
%   uav_llh     - [n×3] 无人机轨迹 [纬度(°), 经度(°), 椭球高(m)]，即 SRT 的 lat/lon/abs_alt
%   radar_llh   - [1×3] 雷达位置 [纬度(°), 经度(°), 椭球高(m)]
%   bearing_deg - 标量，阵面法向的罗盘朝向(顺时针自正北, °)，例 182
%
% 输出:
%   xyz - [n×3] 雷达相对坐标 [x, y, z] (m)
%         x = 距离向(阵面法向, 正前方)，y = 横向(正=阵面左侧)，z = 向上
%         面朝阵面法向(方位 bearing_deg)向外看时 y>0 在左手边，与差通道"左减右"约定一致
%         (偏左 ⇒ az>0 ⇒ Δaz/Σ 落在 φ_az 方向，故 sign_az=+1)。见 run_batch_pipeline 的
%         cfg.angle.phi_az / sign_az —— 注意侧别要靠 φ_az 投影后才取符号，不能直接取 real()。
%         与 tracker_3D_EKF 一致: az=atan2(y,x), el=atan2(z,sqrt(x^2+y^2))
%
% 原理: LLH → ECEF → ENU(以雷达为原点) → 绕天轴旋转到阵面朝向。
% 无 Mapping/Navigation 工具箱依赖，纯公式实现。

    if nargin < 3, bearing_deg = 0; end

    % ---- WGS84 椭球参数 ----
    a  = 6378137.0;
    f  = 1 / 298.257223563;
    e2 = f * (2 - f);

    % ---- 雷达原点 ECEF 与参考经纬度 ----
    [r_x, r_y, r_z] = geodetic2ecef(radar_llh(1), radar_llh(2), radar_llh(3), a, e2);
    lat0 = deg2rad(radar_llh(1));
    lon0 = deg2rad(radar_llh(2));

    % ---- UAV ECEF ----
    [u_x, u_y, u_z] = geodetic2ecef(uav_llh(:,1), uav_llh(:,2), uav_llh(:,3), a, e2);

    % ---- ECEF → ENU (雷达为原点) ----
    dx = u_x - r_x;
    dy = u_y - r_y;
    dz = u_z - r_z;
    E = -sin(lon0) .* dx + cos(lon0) .* dy;
    N = -sin(lat0) .* cos(lon0) .* dx - sin(lat0) .* sin(lon0) .* dy + cos(lat0) .* dz;
    U =  cos(lat0) .* cos(lon0) .* dx + cos(lat0) .* sin(lon0) .* dy + sin(lat0) .* dz;

    % ---- ENU → 雷达帧 (X=距离向, Y=阵面左侧, Z=上) ----
    b = deg2rad(bearing_deg);
    x =  sin(b) .* E + cos(b) .* N;   % 阵面法向(方位 b)
    y = -cos(b) .* E + sin(b) .* N;   % 阵面左侧(方位 b-90°): 实测左右镜像修正后落在此方向
    z =  U;

    xyz = [x, y, z];
end

function [x, y, z] = geodetic2ecef(lat, lon, h, a, e2)
%GEODETIC2ECEF WGS84 大地坐标 → 地心地固直角坐标 (支持向量)。
    lat_r = deg2rad(lat);
    lon_r = deg2rad(lon);
    N = a ./ sqrt(1 - e2 .* sin(lat_r).^2);
    x = (N + h) .* cos(lat_r) .* cos(lon_r);
    y = (N + h) .* cos(lat_r) .* sin(lon_r);
    z = (N .* (1 - e2) + h) .* sin(lat_r);
end
