# syntax=docker/dockerfile:1
ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest
FROM ${ALGO_BASE}

WORKDIR /app
COPY src/ ./src/

RUN cmake -S src -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
    && cmake --build build --parallel \
    && cp build/pulsecompression ./pulsecompression \
    && rm -rf src build

ENTRYPOINT ["/app/pulsecompression"]

# Image contract: keep these four labels unchanged for PulseCompression.
LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="operator" \
      io.uestcradar.input="1:3" \
      io.uestcradar.output="2:2"
