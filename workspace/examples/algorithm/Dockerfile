# syntax=docker/dockerfile:1
ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest
FROM ${ALGO_BASE}

WORKDIR /app
COPY src/ ./src/

# 编译算法业务代码
RUN cmake -S src -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel \
    && cp build/algorithm ./algorithm \
    && rm -rf src build

ENTRYPOINT ["/app/algorithm"]

LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="operator" \
      io.uestcradar.input="1:3" \
      io.uestcradar.output="2:2"
