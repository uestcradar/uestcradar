# syntax=docker/dockerfile:1
ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest
FROM ${ALGO_BASE}

WORKDIR /app
COPY src/ ./src/
COPY CMakeLists.txt ./

# 仅仅编译 Cascade Worker 算子业务代码（秒级构建，不重新编译 SDK）
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel \
    && cp build/cascade-worker ./cascade-worker \
    && rm -rf build CMakeLists.txt src

ENTRYPOINT ["/app/cascade-worker"]
