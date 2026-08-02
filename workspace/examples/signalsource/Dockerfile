# syntax=docker/dockerfile:1
ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest
FROM ${ALGO_BASE}

WORKDIR /app
COPY src/ ./src/

# 编译模拟源业务代码
RUN cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build --parallel \
    && cp build/signalsource ./signalsource \
    && cp build/frame-sink ./frame-sink \
    && rm -rf src build

ENTRYPOINT ["/app/signalsource"]

LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="source" \
      io.uestcradar.input="none" \
      io.uestcradar.output="1:2"
