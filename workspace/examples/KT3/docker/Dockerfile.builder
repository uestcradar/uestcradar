ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest

FROM ${ALGO_BASE} AS builder

RUN sed -i \
        's|http://ports.ubuntu.com/ubuntu-ports/|http://mirrors.aliyun.com/ubuntu-ports/|g' \
        /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get update && apt-get install -y --no-install-recommends \
    qtbase5-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY support/ ./support/
COPY tests/ ./tests/

RUN cmake -S /src -B /build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=ON \
        -DBUILD_INFRA=OFF \
    && cmake --build /build --parallel \
        --target qt5-algorithm rd-algorithm-test \
    && ctest --test-dir /build --output-on-failure

