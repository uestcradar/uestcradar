# ========================================================================
# 🐳 uestcradar 算子模板专属隔离编译开发底座 (Dockerfile)
# 用于支持该模板工程自闭环地在容器内构建 X86 二进制动态插件
# ========================================================================
ARG BASE_IMAGE=docker.m.daocloud.io/library/ubuntu:24.04
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

# 安装 CMake 与 C++ 编译基础链
RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends \
        build-essential cmake && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*
