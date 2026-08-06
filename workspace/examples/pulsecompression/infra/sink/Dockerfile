# syntax=docker/dockerfile:1
ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest
FROM ${ALGO_BASE}

WORKDIR /app
COPY CMakeLists.txt ./CMakeLists.txt
COPY src/ ./src/

RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=ON \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure \
    && cp build/signalsink /app/signalsink \
    && rm -rf CMakeLists.txt src build

ENTRYPOINT ["/app/signalsink"]

LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="sink" \
      io.uestcradar.input="2:2" \
      io.uestcradar.output="none"
