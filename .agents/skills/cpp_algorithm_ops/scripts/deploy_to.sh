#!/bin/bash
# ========================================================================
# C++ 雷达算子工业级四步安全部署与熔断工具链
# Step 1: 环境分析 -> Step 2: Docker 隔离编译 -> Step 3: 熔断测试 -> Step 4: 物理分发
# Usage: bash deploy_to.sh <node_id> (如: node2 或 node4)
# ========================================================================
set -eo pipefail

NODE="${1:-}"
if [ -z "$NODE" ]; then
    echo -e "\033[31m[ERROR] 请指定目标部署节点 (例如: node2 或 node4)\033[0m"
    exit 1
fi

# ------------------------------------------------------------------------
# 📌 第一步: 确定与解析环境变量 (物理节点拓扑与内核架构定义)
# ------------------------------------------------------------------------
CYCORE_ENV="/home/zikun/code/common/cycore/.env.local"
if [ ! -f "$CYCORE_ENV" ]; then
    echo -e "\033[31m[ERROR] 找不到 cycore 的集群配置文件: $CYCORE_ENV\033[0m"
    exit 1
fi
source "$CYCORE_ENV"

# 动态映射获取物理参数
ip_var="CYCORE_DU_${NODE}_HOST_IP"
pass_var="CYCORE_DU_${NODE}_HOST_PASS"
dir_var="CYCORE_DU_${NODE}_HOST_DIR"
user_var="CYCORE_DU_${NODE}_HOST_USER"

REMOTE_IP=$(echo "${!ip_var}" | tr -d '\r' | tr -d '"' | tr -d "'")
REMOTE_PASS=$(echo "${!pass_var}" | tr -d '\r' | tr -d '"' | tr -d "'")
CODE_DIR=$(echo "${!dir_var}" | tr -d '\r' | tr -d '"' | tr -d "'")
REMOTE_USER="${!user_var:-root}"

# 架构和 Docker 参数识别
USE_DOCKER="true" # 雷达组件默认均在容器中运行
DOCKER_NAME="sim-du-${NODE}"
ARCH="arm"        # 默认 Node2 / Node4 为 AArch64 架构板卡

if [ -z "$REMOTE_IP" ] || [ -z "$REMOTE_PASS" ]; then
    echo -e "\033[31m[ERROR] 节点 $NODE 的物理连接参数缺失！\033[0m"
    exit 1
fi

UESTCRADAR_REMOTE_DIR="/home/workspace/uestcradar"
LOCAL_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../" && pwd)"

# 执行 rsync 代码同步至目标物理机工作区
echo -e "\033[32m=== [Step 1] 同步 uestcradar 算法源码至远端宿主机 [$NODE] (${REMOTE_IP}) ===\033[0m"
export SSHPASS="${REMOTE_PASS}"
rsync -avz --delete --exclude="cpp/build" --exclude=".git" \
  -e "sshpass -e ssh -o StrictHostKeyChecking=no" \
  "${LOCAL_ROOT}/" \
  "${REMOTE_USER}@${REMOTE_IP}:${UESTCRADAR_REMOTE_DIR}/"

# ------------------------------------------------------------------------
# 📌 第二步: 基于架构选择 Dockerfile 并在容器内进行隔离编译
# ------------------------------------------------------------------------
echo -e "\033[32m=== [Step 2] 判定硬件架构 [${ARCH}]，利用远端容器执行隔离编译 ===\033[0m"

# 根据 ARCH 选择编译时的环境镜像，Node2/Node4 已预装 cycore-build
COMPILER_IMAGE="cycore-build:latest"
if [ "${ARCH}" = "arm" ]; then
    # 若有专门针对 arm 优化或交叉编译的 Tag，可在此切换，当前通用 cycore-build
    COMPILER_IMAGE="cycore-build:latest"
fi

# 执行远端 Docker 隔离编译
sshpass -e ssh -o StrictHostKeyChecking=no "${REMOTE_USER}@${REMOTE_IP}" "
  docker run --rm \
    --user '0:0' \
    -v ${UESTCRADAR_REMOTE_DIR}:/workspace \
    -w /workspace/cpp \
    ${COMPILER_IMAGE} \
    bash -lc '
      mkdir -p build && cd build && \
      cmake .. -DCMAKE_BUILD_TYPE=Release && \
      make -j\$(nproc)
    '
"

# ------------------------------------------------------------------------
# 📌 第三步: 容器内运行算子单元测试与前置熔断 (Test-First Shield)
# ------------------------------------------------------------------------
echo -e "\033[32m=== [Step 3] 触发前置测试防线，在容器内强制执行算子单元测试 ===\033[0m"

# 我们在编译镜像内执行我们刚刚编写好并分流的各个算子测试程序 (例如 FFT 算子单测)
# 如果测试执行失败，则利用 exit code 机制在 SSH 层面触发熔断
set +e
sshpass -e ssh -o StrictHostKeyChecking=no "${REMOTE_USER}@${REMOTE_IP}" "
  docker run --rm \
    --user '0:0' \
    -v ${UESTCRADAR_REMOTE_DIR}:/workspace \
    -w /workspace/cpp/build \
    ${COMPILER_IMAGE} \
    bash -lc '
      echo \"--> Running FFT/IFFT algorithm simulation tests inside container...\" && \
      ./bin/cycore_fft_plugin || exit 1
    '
"
TEST_STATUS=$?
set -e

# 🛑 触发熔断检查
if [ $TEST_STATUS -ne 0 ]; then
    echo -e "\033[31m[FATAL ERROR] 单元测试执行失败！\033[0m"
    echo -e "\033[31m[FATAL ERROR] 已触发前置熔断防御：终止部署流程，保留物理现场以供人类排查。\033[0m"
    exit 2
else
    echo -e "\033[32m[SUCCESS] 单元测试全数通过！允许进入物理覆盖分发阶段。\033[0m"
fi

# ------------------------------------------------------------------------
# 📌 第四步: 物理覆盖部署 (Deploy)
# ------------------------------------------------------------------------
echo -e "\033[32m=== [Step 4] 物理覆盖部署：将经过验证的 .so 插件装载至目标运行容器 ===\033[0m"

# 复制到 cycore 的插件挂载目录中
sshpass -e ssh -o StrictHostKeyChecking=no "${REMOTE_USER}@${REMOTE_IP}" "
  cp ${UESTCRADAR_REMOTE_DIR}/cpp/build/lib/*.so ${CODE_DIR}/lib/du/flowgraph/plugins/
"

if [ "${USE_DOCKER}" = "true" ]; then
    echo -e "\033[32m--> 重启目标容器 ${DOCKER_NAME} 以热重载全新算子插件...\033[0m"
    sshpass -e ssh -o StrictHostKeyChecking=no "${REMOTE_USER}@${REMOTE_IP}" "
      docker restart ${DOCKER_NAME}
    "
fi

echo -e "\033[32m==================================================\033[0m"
echo -e "\033[32m[SUCCESS] 节点 $NODE 算子安全物理部署热重载成功！\033[0m"
echo -e "\033[32m==================================================\033[0m"
