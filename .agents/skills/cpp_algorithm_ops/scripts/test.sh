#!/bin/bash
# ========================================================================
# 🧪 2. test.sh: 本地容器单测自检与前置熔断脚本 (完全对齐 SKILL 决策流 Phase 3)
# Usage: bash test.sh
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

# 📌 2. 检查前置编译产物是否存在 (Phase 2)
# 单测运行在本地 X86 上，检验 build 目录下是否有测试二进制文件
TEST_BIN="${LOCAL_ROOT}/cpp/build/bin/cycore_fft_plugin"
if [ ! -f "$TEST_BIN" ]; then
    echo -e "\033[31m[🚨 中止: 编译产物缺失] 未发现 X86 编译产物，请确保在本地进行了同构编译！\033[0m"
    exit 1
fi

COMPILER_IMAGE="uestcradar-build:local"

echo -e "\033[32m=== [Test] 挂载本地工作区并在隔离容器内执行单测自检 (Phase 3) ===\033[0m"

set +e
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "${LOCAL_ROOT}:/workspace" \
  -w /workspace/cpp \
  ${COMPILER_IMAGE} \
  bash -lc "./build/bin/cycore_fft_plugin"
TEST_STATUS=$?
set -e

# 📌 3. 前置测试结果判定
if [ $TEST_STATUS -ne 0 ]; then
    echo -e "\033[31m[🚨 熔断: 单测断言报错] 单元测试执行失败！已触发前置熔断防御：严禁分发部署！\033[0m"
    exit 2
else
    echo -e "\033[32m[SUCCESS] 本地单元测试 100% 通过！允许进入下一阶段。\033[0m"
fi
