# syntax=docker/dockerfile:1
ARG BASE_IMAGE=registry.chengyistudio.com/cxx/ubuntu:24.04
FROM ${BASE_IMAGE} AS builder

# 1. 安装编译所需的工具链
ENV DEBIAN_FRONTEND=noninteractive
RUN unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends cmake g++ make \
    && rm -rf /var/lib/apt/lists/*

# 2. 拷贝底层依赖与 SDK 源码并执行编译
WORKDIR /src
COPY workspace/common/ ./common/
COPY workspace/sdk/ ./sdk/
RUN cmake -S sdk -B sdk-build \
       -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_INSTALL_PREFIX=/out/sdk \
    && cmake --build sdk-build --parallel \
    && cmake --install sdk-build

# ==============================================================================
# 最终交付的“算法底层基座镜像”
# ==============================================================================
FROM ${BASE_IMAGE} AS algo-base

ENV DEBIAN_FRONTEND=noninteractive

# 1. 算法开发者依旧需要 g++ 和 make 来编译他们自己的业务代码
RUN unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends g++ make cmake \
    && rm -rf /var/lib/apt/lists/*

# 2. 将整个 SDK 产物（头文件、动态库、CMake Config）合并进系统目录
COPY --from=builder /out/sdk/ /usr/local/
RUN ldconfig

# 设置标准工作目录，等待算法开发者写入
WORKDIR /app
