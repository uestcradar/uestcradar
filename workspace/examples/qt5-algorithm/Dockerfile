# ==============================================================================
# 第一阶段：编译阶段 (Builder)
# 基于最新发布的 algo-base 镜像，该镜像已预装了 cycomm_sdk 头文件和 CMake 配置
# ==============================================================================
FROM registry.chengyistudio.com/cxx/algo-base:latest AS builder

# 安装 Qt5 Core 开发包
RUN apt-get update && apt-get install -y --no-install-recommends \
    qtbase5-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

WORKDIR /build
# 使用 CMake 编译 Qt5 工程
RUN cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --parallel

# ==============================================================================
# 第二阶段：运行阶段 (Runtime)
# 同样基于 algo-base，保证底层运行时库的一致性，补充 Qt5 运行库
# ==============================================================================
FROM registry.chengyistudio.com/cxx/algo-base:latest

# 仅安装运行所需的库 (libqt5core5a)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libqt5core5a \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /build/qt5-algorithm .

# 设定程序的入口点
ENTRYPOINT ["/app/qt5-algorithm"]
