#!/usr/bin/env bash
# Usage:
#   ./workspace/tools/sync_distributed_compose.sh
# Purpose:
#   将 workspace/tools/compose.cascade.distributed.yaml 一键同步更新到所有集群节点的 /root/workspace/docker/ 目录下
# Security Rules:
#   - 默认密码为空（优先使用 SSH Key 认证）
#   - 可通过环境变量 CXX_SERVER_SSH_PASSWORD 显式传入密码
#   - StrictHostKeyChecking 使用 accept-new 安全接受新指纹
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_file="${script_dir}/compose.cascade.distributed.yaml"
target_dir="/root/workspace/docker"
target_file="${target_dir}/compose.cascade.distributed.yaml"

DEFAULT_NODES=(
  192.162.2.16
  192.162.2.32
  192.162.2.64
  192.162.2.80
  192.162.2.128
  192.162.2.144
  192.162.2.160
  192.162.2.176
  192.162.2.192
)

SSH_USER="${CXX_SERVER_SSH_USER:-root}"
SSH_PASSWORD="${CXX_SERVER_SSH_PASSWORD:-}"

if [[ ! -f "${source_file}" ]]; then
  echo "error: 源文件 ${source_file} 不存在" >&2
  exit 1
fi

if [[ -n "${SSH_PASSWORD}" ]]; then
  if ! command -v sshpass >/dev/null 2>&1; then
    echo "error: 当提供 CXX_SERVER_SSH_PASSWORD 时需要安装 sshpass" >&2
    exit 1
  fi
  run_scp() {
    SSHPASS="${SSH_PASSWORD}" sshpass -e scp \
      -o StrictHostKeyChecking=accept-new \
      -o LogLevel=ERROR \
      -o ConnectTimeout=8 \
      "$@"
  }
  run_ssh() {
    SSHPASS="${SSH_PASSWORD}" sshpass -e ssh \
      -o StrictHostKeyChecking=accept-new \
      -o LogLevel=ERROR \
      -o ConnectTimeout=8 \
      "$@"
  }
else
  run_scp() {
    scp \
      -o StrictHostKeyChecking=accept-new \
      -o LogLevel=ERROR \
      -o ConnectTimeout=8 \
      "$@"
  }
  run_ssh() {
    ssh -x \
      -o StrictHostKeyChecking=accept-new \
      -o LogLevel=ERROR \
      -o ConnectTimeout=8 \
      "$@"
  }
fi

success_nodes=()
failed_nodes=()

echo "==> 开始一键分发 ${source_file} 到所有集群节点的 ${target_file}"
echo "    SSH 用户: ${SSH_USER}"
echo "    目标节点数: ${#DEFAULT_NODES[@]}"
echo

for node in "${DEFAULT_NODES[@]}"; do
  echo -n "==> 同步至节点 [${node}]... "
  if run_ssh "${SSH_USER}@${node}" "mkdir -p '${target_dir}'" && \
     run_scp "${source_file}" "${SSH_USER}@${node}:${target_file}"; then
    echo "✔ 成功"
    success_nodes+=("${node}")
  else
    echo "✖ 失败" >&2
    failed_nodes+=("${node}")
  fi
done

echo
echo "========================================================="
echo " 集群同步完成汇总报告"
echo "========================================================="
echo " 成功节点数 (${#success_nodes[@]}): ${success_nodes[*]:-无}"
echo " 失败节点数 (${#failed_nodes[@]}): ${failed_nodes[*]:-无}"
echo "========================================================="

if [[ "${#failed_nodes[@]}" -gt 0 ]]; then
  exit 1
fi
