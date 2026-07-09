#!/bin/bash
# ========================================================================
# 🔨 1. build.sh: 算子同构/交叉编译脚本 (完全对齐 SKILL 决策流 Phase 2)
# Usage: bash build.sh
# ========================================================================
set -eo pipefail

LOCAL_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../" && pwd)"

# 📌 1. 读取专属部署配置文件 .env (Phase 1)
LOCAL_ENV="${LOCAL_ROOT}/.env"
if [ ! -f "$LOCAL_ENV" ]; then
    # 兼容在子目录运行的情况
    LOCAL_ENV="$(cd "$(dirname "${BASH_SOURCE[0]}")/../" && pwd)/.env"
fi

if [ ! -f "$LOCAL_ENV" ]; then
    echo -e "\033[31m[🚨 中止: 环境参数缺失] 找不到专属配置文件 .env，请基于 env.template 拷贝创建！\033[0m"
    exit 1
fi
source "$LOCAL_ENV"

if [ -z "${ARCH}" ]; then
    echo -e "\033[31m[🚨 中止: 环境参数缺失] 配置文件中未定义 ARCH 变量！\033[0m"
    exit 1
fi

# 📌 2. 硬件架构分流判定 (Phase 2)
if [ "${ARCH}" = "x86" ]; then
    echo -e "\033[32m--> 识别为 [ARCH = x86]，走本地同构编译分支...\033[0m"
    COMPILER_IMAGE="uestcradar-build:local"
    
    # 自动判定并自建本地自检镜像
    if ! docker image inspect ${COMPILER_IMAGE} &>/dev/null; then
        echo "--> 本地未检测到编译底座 ${COMPILER_IMAGE}，开始构建..."
        docker build -t ${COMPILER_IMAGE} \
          -f "${LOCAL_ROOT}/.agents/skills/cpp_algorithm_ops/Dockerfile.build" \
          "${LOCAL_ROOT}/.agents/skills/cpp_algorithm_ops"
    fi
    
    # 执行本地隔离编译
    docker run --rm \
      --user "$(id -u):$(id -g)" \
      -v "${LOCAL_ROOT}:/workspace" \
      -w /workspace/cpp \
      ${COMPILER_IMAGE} \
      bash -lc "cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
      
elif [ "${ARCH}" = "arm" ]; then
    echo -e "\033[32m--> 识别为 [ARCH = arm]，走跨平台交叉编译分支...\033[0m"
    COMPILER_IMAGE="uestcradar-build:cross"
    
    # 自动判定并自建交叉编译镜像
    if ! docker image inspect ${COMPILER_IMAGE} &>/dev/null; then
        echo "--> 本地未检测到交叉编译底座 ${COMPILER_IMAGE}，开始构建..."
        docker build -t ${COMPILER_IMAGE} \
          -f "${LOCAL_ROOT}/.agents/skills/cpp_algorithm_ops/Dockerfile.build_cross" \
          "${LOCAL_ROOT}/.agents/skills/cpp_algorithm_ops"
    fi
    
    # 执行本地交叉编译
    docker run --rm \
      --user "$(id -u):$(id -g)" \
      -v "${LOCAL_ROOT}:/workspace" \
      -w /workspace/cpp \
      ${COMPILER_IMAGE} \
      bash -lc "cmake -B build_cross -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc && cmake --build build_cross -j\$(nproc)"
      
else
    echo -e "\033[31m[🚨 中止: 编译构建失败] 不支持的 ARCH 架构类型: ${ARCH}\033[0m"
    exit 1
fi

echo -e "\033[32m[SUCCESS] 本地编译分支构建成功！\033[0m"
