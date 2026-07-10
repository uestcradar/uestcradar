#!/bin/bash
# ========================================================================
# 🚀 3. deploy.sh: 本地交叉编译产物跨网络分发与热重启脚本 (完全对齐 SKILL 决策流 Phase 3.5 & 4)
# Usage: bash deploy.sh
# ========================================================================
set -eo pipefail

LOCAL_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../" && pwd)"

# 📌 1. 读取专属部署配置文件 .env (Phase 1)
LOCAL_ENV="${LOCAL_ROOT}/.env"
if [ ! -f "$LOCAL_ENV" ]; then
    LOCAL_ENV="$(cd "$(dirname "${BASH_SOURCE[0]}")/../" && pwd)/.env"
fi

if [ ! -f "$LOCAL_ENV" ]; then
    echo -e "\033[31m[🚨 中止: 环境参数缺失] 找不到专属配置文件 .env，请基于 env.template 拷贝创建！\033[0m"
    exit 1
fi
source "$LOCAL_ENV"

if [ -z "${REMOTE_IP}" ] || [ -z "${REMOTE_PASS}" ] || [ -z "${DOCKER_NAME}" ]; then
    echo -e "\033[31m[🚨 中止: 环境参数缺失] 专属 .env 配置文件未配置完整（需包含 REMOTE_IP, REMOTE_PASS, DOCKER_NAME）！\033[0m"
    exit 1
fi

REMOTE_USER="${REMOTE_USER:-root}"
CODE_DIR="${CODE_DIR:-/home/workspace/cycore}"
USE_DOCKER="${USE_DOCKER:-true}"

export SSHPASS="${REMOTE_PASS}"

# 📌 2. 检查编译产物是否存在 (Phase 2)
LOCAL_SO_DIR="${LOCAL_ROOT}/cpp/build_cross/lib"
if [ ! -d "$LOCAL_SO_DIR" ] || [ -z "$(ls -A "$LOCAL_SO_DIR" 2>/dev/null)" ]; then
    echo -e "\033[31m[🚨 中止: 编译产物缺失] 本地未发现交叉编译产物！请先在本地运行 build.sh 编译！\033[0m"
    exit 1
fi

# 📌 3. 拓扑与参数类型静态校验 (Phase 3.5)
echo -e "\033[32m=== [Check] 执行流图拓扑合规性与参数类型静态校验 (Phase 3.5) ===\033[0m"

# 自动从 DOCKER_NAME 中提取目标节点名称 (如: sim-du-node2 -> node2)
NODE_NAME=$(echo "${DOCKER_NAME}" | sed 's/sim-du-//')
YAML_FILE="/home/zikun/code/common/cycore/lib/du/app/${NODE_NAME}_graph.yaml"
if [ ! -f "$YAML_FILE" ]; then
    YAML_FILE="/home/zikun/code/common/cycore/lib/du/app/default_graph.yaml"
fi

if [ ! -f "$YAML_FILE" ]; then
    echo -e "\033[31m[🚨 熔断: 拓扑或参数类型错误] 找不到任何 YAML 拓扑配置文件！\033[0m"
    exit 3
fi

echo "--> 正在核对本地 YAML 拓扑配置文件: $YAML_FILE"

# (1) 提取算子 Block 并检查 params 属性及类型
PLUGIN_BLOCK=$(awk '/- name:/{flag=0} /plugin:.*cycore_ifft_plugin/{flag=1} flag' "$YAML_FILE")

if [ -n "$PLUGIN_BLOCK" ]; then
    # 检查是否包含 params:
    if ! echo "$PLUGIN_BLOCK" | grep -q "params:"; then
        echo -e "\033[31m[🚨 熔断: 拓扑或参数类型错误] 算子定义块中缺失必要的 'params:' 初始化字段！\033[0m"
        echo "出错上下文:"
        echo "$PLUGIN_BLOCK"
        exit 3
    fi
    
    # 检查核心运行参数 fft_size 类型是否为正整数
    FFT_SIZE_VAL=$(echo "$PLUGIN_BLOCK" | awk '/fft_size:/{print $2}' | tr -d '\r' | tr -d '"' | tr -d "'")
    if [ -z "$FFT_SIZE_VAL" ] || [[ ! "$FFT_SIZE_VAL" =~ ^[0-9]+$ ]]; then
        echo -e "\033[31m[🚨 熔断: 拓扑或参数类型错误] 初始化参数 'fft_size' 缺失或类型错误（应为正整数，实际为: '$FFT_SIZE_VAL'）！\033[0m"
        echo "出错上下文:"
        echo "$PLUGIN_BLOCK"
        exit 3
    fi
fi

# (2) 缓冲容量防死锁校验 (防止 capacity < 1024 的死锁风险)
CAPACITY_VAL=$(awk '/- name:.*ifft_to_sink/{flag=1} flag && /capacity:/{print $2; exit}' "$YAML_FILE" | tr -d '\r' | tr -d '"' | tr -d "'")
if [ -n "$CAPACITY_VAL" ]; then
    if [ "$CAPACITY_VAL" -lt 1024 ]; then
        echo -e "\033[31m[🚨 熔断: 拓扑或参数类型错误] 连接线容量过小 ($CAPACITY_VAL < 1024)，面临流图读写死锁风险！\033[0m"
        exit 3
    fi
fi

echo -e "\033[32m[SUCCESS] 拓扑与参数校验 100% 合规，准许进行物理部署分发。\033[0m"

# 📌 4. 执行物理覆盖部署与热重启 (Phase 4)
echo -e "\033[32m=== [Deploy] 跨网络分发已编译插件至目标节点 (${REMOTE_IP}) ===\033[0m"

if [ "${USE_DOCKER}" = "false" ]; then
    echo -e "--> 识别为 [物理机部署] (USE_DOCKER=false)，直接 SCP 传输至远端物理插件路径..."
    sshpass -e scp -o StrictHostKeyChecking=no "${LOCAL_SO_DIR}/"*.so "${REMOTE_USER}@${REMOTE_IP}:${CODE_DIR}/lib/du/flowgraph/plugins/"
else
    echo -e "--> 识别为 [Docker 部署] (USE_DOCKER=true)，检测目标容器挂载属性..."
    
    # 动态检测目标容器是否在物理宿主机上挂载了 plugins 目录
    HAS_MOUNT=$(sshpass -e ssh -o StrictHostKeyChecking=no "${REMOTE_USER}@${REMOTE_IP}" "
      docker inspect --format='{{range .Mounts}}{{.Destination}}{{end}}' ${DOCKER_NAME} | grep -q '/workspace/lib/du/flowgraph/plugins' && echo 'true' || echo 'false'
    ")
    
    if [ "$HAS_MOUNT" = "true" ]; then
        echo -e "  [方案 A] 目标容器已挂载物理卷，SCP 直推拷贝至宿主机挂载目录并重启容器..."
        sshpass -e scp -o StrictHostKeyChecking=no "${LOCAL_SO_DIR}/"*.so "${REMOTE_USER}@${REMOTE_IP}:${CODE_DIR}/lib/du/flowgraph/plugins/"
        sshpass -e ssh -o StrictHostKeyChecking=no "${REMOTE_USER}@${REMOTE_IP}" "docker restart ${DOCKER_NAME}"
    else
        echo -e "  [方案 B] 目标容器未挂载卷，SCP 传输至临时缓存并强制热注入重启..."
        # 1. 拷贝至宿主机临时区
        sshpass -e scp -o StrictHostKeyChecking=no "${LOCAL_SO_DIR}/"*.so "${REMOTE_USER}@${REMOTE_IP}:/root/"
        # 2. 热注入并赋权重启
        sshpass -e ssh -o StrictHostKeyChecking=no "${REMOTE_USER}@${REMOTE_IP}" "
          docker exec ${DOCKER_NAME} mkdir -p /workspace/lib/du/flowgraph/plugins/ && \
          docker cp /root/. ${DOCKER_NAME}:/workspace/lib/du/flowgraph/plugins/ && \
          docker exec ${DOCKER_NAME} chmod +x /workspace/lib/du/flowgraph/plugins/*.so && \
          rm -f /root/*.so && \
          docker restart ${DOCKER_NAME}
        "
    fi
fi

echo -e "\033[32m==================================================\033[0m"
echo -e "\033[32m[SUCCESS] 本地交叉编译产物热部署重启成功！\033[0m"
echo -e "\033[32m==================================================\033[0m"
