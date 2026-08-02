# syntax=docker/dockerfile:1
ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest
FROM ${ALGO_BASE}

WORKDIR /app
COPY src/ ./src/
COPY entrypoint.sh ./entrypoint.sh

# 编译模拟源业务代码
RUN cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build --parallel \
    && cp build/signalsource ./signalsource \
    && cp build/pulse-source ./pulse-source \
    && cp build/frame-sink ./frame-sink \
    && chmod +x ./entrypoint.sh \
    && rm -rf src build

ENTRYPOINT ["/app/entrypoint.sh"]

LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="source,sink" \
      io.uestcradar.input="2:2" \
      io.uestcradar.output="1:3"
