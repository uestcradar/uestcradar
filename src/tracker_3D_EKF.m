function [tracks, track_id_counter] = tracker_3D_EKF(tracks, raw_meas, track_id_counter, tracker_params)
%TRACKER_3D_EKF 三维扩展卡尔曼滤波(EKF)多目标跟踪器。
%
%   本函数实现完整的多目标跟踪，采用笛卡尔坐标系 EKF，处理极坐标量测。
%   跟踪流程：预测 → 关联(GNN) → 更新 → 起始 → M/N确认/终止。
%
%   状态向量 (6x1): [px; vx; py; vy; pz; vz]   (笛卡尔, m, m/s)
%   量测向量 (4x1): [range(m); azimuth(rad); elevation(rad); range_rate(m/s)]
%
%   输入:
%   tracks           : struct array - 当前所有航迹（首次调用为空模板）。
%   raw_meas         : [M x 5] - 当前帧融合后量测 [r, az_rad, el_rad, vr, time]。
%   track_id_counter : integer - 全局航迹 ID 计数器。
%   tracker_params   : struct - 跟踪器参数（见下方字段说明）。
%
%   输出:
%   tracks           : struct array - 更新后的航迹。
%   track_id_counter : integer - 更新后的 ID 计数器。

%% 1. 参数提取
T = tracker_params.dt_frame;
R_mat = tracker_params.measurement_noise;
Q = tracker_params.process_noise_matrix;
h_radar = tracker_params.radar_height;

% 状态转移矩阵 F (CV 模型)
F = [1, T, 0, 0, 0, 0;
     0, 1, 0, 0, 0, 0;
     0, 0, 1, T, 0, 0;
     0, 0, 0, 1, 0, 0;
     0, 0, 0, 0, 1, T;
     0, 0, 0, 0, 0, 1];

current_time = NaN;
if ~isempty(raw_meas), current_time = raw_meas(1, 5); end

%% 2. 航迹状态预测
for i = 1:numel(tracks)
    if ~tracks(i).is_terminated
        tracks(i).state = F * tracks(i).state;
        tracks(i).covariance = F * tracks(i).covariance * F' + Q;
        tracks(i).prediction_count = tracks(i).prediction_count + 1;
    end
end

%% 3. 数据关联 (GNN, 量测空间马氏距离)
num_tracks = numel(tracks);
num_meas = size(raw_meas, 1);
associations = zeros(1, num_meas);
track_asso_info = cell(num_tracks, 1);

if num_tracks > 0 && num_meas > 0
    cost_matrix = inf(num_tracks, num_meas);

    % --- 3.1 预计算各航迹的 h(x), H, S ---
    for t = 1:num_tracks
        if tracks(t).is_terminated, continue; end
        x_pred = tracks(t).state;
        [hx, H] = measurement_jacobian(x_pred, h_radar);
        if any(isnan(hx)) || any(isnan(H(:))), continue; end

        S = H * tracks(t).covariance * H' + R_mat;

        gate_confidence = tracker_params.gate_confidence;
        if tracks(t).success_count < 3
            gate_confidence = 0.999;  % 新生航迹放宽门限
        end
        gate_threshold_sq = chi2inv(gate_confidence, 4);

        track_asso_info{t} = struct('hx', hx, 'H', H, 'S', S, 'gate_sq', gate_threshold_sq);
    end

    % --- 3.2 计算代价矩阵 (马氏距离平方) ---
    for t = 1:num_tracks
        if isempty(track_asso_info{t}), continue; end
        info = track_asso_info{t};
        S_inv = inv(info.S);

        for m = 1:num_meas
            z_m = raw_meas(m, 1:4)';
            innov = z_m - info.hx;
            innov(2:3) = atan2(sin(innov(2:3)), cos(innov(2:3)));  % 角度包裹

            md_sq = innov' * S_inv * innov;
            if md_sq <= info.gate_sq
                cost_matrix(t, m) = md_sq;
            end
        end
    end

    % --- 3.3 贪婪 GNN 分配 ---
    for iter = 1:min(num_tracks, num_meas)
        [min_val, min_idx] = min(cost_matrix(:));
        if isinf(min_val), break; end
        [t_idx, m_idx] = ind2sub(size(cost_matrix), min_idx);
        associations(m_idx) = t_idx;
        cost_matrix(t_idx, :) = inf;
        cost_matrix(:, m_idx) = inf;
    end
end

unassociated_meas_indices = find(associations == 0);
unassociated_meas = raw_meas(unassociated_meas_indices, :);

%% 4. 航迹更新与历史记录
for t = 1:numel(tracks)
    if tracks(t).is_terminated, continue; end

    m_idx = find(associations == t, 1);

    if ~isempty(m_idx)
        % --- 关联成功：EKF 更新 ---
        z = raw_meas(m_idx, 1:4)';
        info = track_asso_info{t};
        if isempty(info), continue; end

        innov = z - info.hx;
        innov(2:3) = atan2(sin(innov(2:3)), cos(innov(2:3)));

        K = tracks(t).covariance * info.H' / info.S;

        tracks(t).state = tracks(t).state + K * innov;

        % Joseph 形式协方差更新
        I6 = eye(6);
        tracks(t).covariance = (I6 - K * info.H) * tracks(t).covariance * (I6 - K * info.H)' + K * R_mat * K';

        tracks(t).consecutive_misses = 0;
        tracks(t).success_count = tracks(t).success_count + 1;
        tracks(t).last_update = current_time;
        tracks(t).prediction_count = 0;
        tracks(t).last_innov = innov;
        tracks(t).last_K = K;
        tracks(t).last_S = info.S;
    else
        % --- 未关联：丢失 ---
        tracks(t).consecutive_misses = tracks(t).consecutive_misses + 1;
        tracks(t).last_innov = [];
        tracks(t).last_K = [];
        tracks(t).last_S = [];
    end

    % --- 历史记录 ---
    tracks(t).total_count = tracks(t).total_count + 1;
    if ~isnan(current_time), tracks(t).timestamps(end + 1) = current_time; end
    tracks(t).path(end + 1, :) = tracks(t).state([1 3 5])';
    tracks(t).velocity_history(end + 1, :) = tracks(t).state([2 4 6])';

    if size(tracks(t).path, 1) > tracker_params.max_history_length
        if ~isempty(tracks(t).timestamps), tracks(t).timestamps(1) = []; end
        tracks(t).path(1, :) = [];
        tracks(t).velocity_history(1, :) = [];
    end

    % --- 连续丢失终止 ---
    if tracks(t).consecutive_misses > tracker_params.max_predictions
        tracks(t).is_terminated = true;
        tracks(t).terminationReason = 'missed';
    end
end

%% 5. 航迹起始
for m = 1:size(unassociated_meas, 1)
    meas_init = unassociated_meas(m, 1:4);
    initial_state = meas_to_state(meas_init, tracker_params.radar_height);
    initial_covariance = compute_initial_covariance(meas_init, tracker_params);

    new_track = struct( ...
        'track_id',          track_id_counter, ...
        'state',             initial_state, ...
        'covariance',        initial_covariance, ...
        'timestamps',        current_time, ...
        'last_update',       current_time, ...
        'consecutive_misses', 0, ...
        'success_count',     1, ...
        'total_count',       1, ...
        'prediction_count',  0, ...
        'is_terminated',     false, ...
        'terminationReason', "", ...
        'is_confirmed',      false, ...
        'confirmed_at',      [], ...
        'path',              initial_state([1 3 5])', ...
        'velocity_history',  initial_state([2 4 6])', ...
        'last_innov',        [], ...
        'last_K',            [], ...
        'last_S',            []);

    tracks = [tracks, new_track]; %#ok<AGROW>
    track_id_counter = track_id_counter + 1;
end

%% 6. M/N 逻辑确认/删除
if ~isempty(tracks)
    is_confirmed = [tracks.success_count] >= tracker_params.M;
    is_old_enough = [tracks.total_count] >= tracker_params.N;
    is_mn_failure = is_old_enough & ~is_confirmed;

    for i = find(is_mn_failure)
        if ~tracks(i).is_terminated
            tracks(i).is_terminated = true;
            tracks(i).terminationReason = 'M/N';
        end
    end

    % 标记已确认航迹
    for i = find(is_old_enough & is_confirmed & ~[tracks.is_confirmed])
        tracks(i).is_confirmed = true;
        tracks(i).confirmed_at = tracks(i).total_count;
    end

    % 清除从未确认的已终止航迹（噪点），回收 ID
    remove_mask = [tracks.is_terminated] & ~[tracks.is_confirmed];
    tracks(remove_mask) = [];
end

end

% =========================================================================
%  辅助子函数
% =========================================================================

function [hx, H] = measurement_jacobian(x_pred, h_radar)
% 计算非线性量测 h(x) 和雅可比矩阵 H = dh/dx。
% 坐标系: X = 主轴(距离向), Y = 横向(方位向), Z = 高度。

px = x_pred(1); vx = x_pred(2);
py = x_pred(3); vy = x_pred(4);
pz = x_pred(5); vz = x_pred(6);

delta_x = px;
delta_y = py;
delta_z = pz - h_radar;

r_g_sq = delta_x^2 + delta_y^2;
r_sq = r_g_sq + delta_z^2;
r = sqrt(r_sq);
r_g = sqrt(r_g_sq);

if r < 1e-6 || r_g < 1e-6
    hx = nan(4, 1); H = nan(4, 6); return;
end

% 径向速度
vr = (delta_x * vx + delta_y * vy + delta_z * vz) / r;

% 量测向量: [r; az; el; vr]
hx = [r; atan2(delta_y, delta_x); atan2(delta_z, r_g); vr];

% 雅可比矩阵 H (4x6)
H = zeros(4, 6);

% d(r)/d(x)
H(1, 1) = delta_x / r;  H(1, 3) = delta_y / r;  H(1, 5) = delta_z / r;

% d(az)/d(x)
H(2, 1) = -delta_y / r_g_sq;
H(2, 3) =  delta_x / r_g_sq;

% d(el)/d(x)
H(3, 1) = -delta_x * delta_z / (r_sq * r_g);
H(3, 3) = -delta_y * delta_z / (r_sq * r_g);
H(3, 5) =  r_g / r_sq;

% d(vr)/d(x)
H(4, 1) = (vx / r) - (vr * delta_x / r_sq);
H(4, 2) = delta_x / r;
H(4, 3) = (vy / r) - (vr * delta_y / r_sq);
H(4, 4) = delta_y / r;
H(4, 5) = (vz / r) - (vr * delta_z / r_sq);
H(4, 6) = delta_z / r;
end

function state = meas_to_state(meas, h_radar)
% 极坐标量测 [r, az_rad, el_rad, vr] → 笛卡尔状态 [x; vx; y; vy; z; vz]。

r = meas(1); az = meas(2); el = meas(3); vr = meas(4);

ce = cos(el); se = sin(el);
ca = cos(az); sa = sin(az);

x_pos = r * ce * ca;
y_pos = r * ce * sa;
z_pos = r * se + h_radar;

los_vec = [ce * ca; ce * sa; se];
v_vec = vr * los_vec;

state = [x_pos; v_vec(1); y_pos; v_vec(2); z_pos; v_vec(3)];
end

function P0 = compute_initial_covariance(meas_init, tracker_params)
% 根据单次极坐标量测计算 6×6 笛卡尔初始协方差 P0。
% 方法：位置协方差通过 Jacobian 变换，速度协方差通过 LOS 坐标系旋转。

r  = meas_init(1); a = meas_init(2); e = meas_init(3);

sigma_r_sq  = tracker_params.measurement_noise(1, 1);
sigma_a_sq  = tracker_params.measurement_noise(2, 2);
sigma_e_sq  = tracker_params.measurement_noise(3, 3);
sigma_vr_sq = tracker_params.measurement_noise(4, 4);

ce = cos(e); se = sin(e);
ca = cos(a); sa = sin(a);

% --- 位置协方差：极坐标 → 笛卡尔 Jacobian 变换 ---
dx_dr = ce * ca;   dx_da = -r * ce * sa;  dx_de = -r * se * ca;
dy_dr = ce * sa;   dy_da =  r * ce * ca;  dy_de = -r * se * sa;
dz_dr = se;        dz_da =  0;            dz_de =  r * ce;

J_g = [dx_dr, dx_da, dx_de;
       dy_dr, dy_da, dy_de;
       dz_dr, dz_da, dz_de];

R_polar_pos = diag([sigma_r_sq, sigma_a_sq, sigma_e_sq]);
P_pos_cart = J_g * R_polar_pos * J_g';

% --- 速度协方差：LOS 坐标系旋转 ---
u_los = [ce * ca; ce * sa; se];

temp_vec = [0; 1; 0];
if abs(dot(u_los, temp_vec)) > 0.99, temp_vec = [1; 0; 0]; end

u_tan1 = cross(u_los, temp_vec);
u_tan1 = u_tan1 / norm(u_tan1);
u_tan2 = cross(u_tan1, u_los);

Rotation_mat = [u_los'; u_tan1'; u_tan2'];

v_tan_std_init = tracker_params.v_tan_std_init;
P_vel_los = diag([sigma_vr_sq, v_tan_std_init^2, v_tan_std_init^2]);
P_vel_cart = Rotation_mat' * P_vel_los * Rotation_mat;

% --- 组合为 6×6 P0 (顺序: x,vx, y,vy, z,vz) ---
P_temp = blkdiag(P_pos_cart, P_vel_cart);
idx = [1, 4, 2, 5, 3, 6];  % [x,y,z,vx,vy,vz] → [x,vx,y,vy,z,vz]
P0 = P_temp(idx, idx);
end
