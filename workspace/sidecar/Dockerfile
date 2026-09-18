# syntax=docker/dockerfile:1

ARG SIDECAR_PLATFORM=linux/arm64
ARG BUILD_BASE=registry.chengyistudio.com/cxx/sidecar:build-base
ARG RUNTIME_BASE=registry.chengyistudio.com/cxx/sidecar:runtime-base

FROM --platform=${SIDECAR_PLATFORM} ${BUILD_BASE} AS build

ARG BUILD_JOBS=2
RUN test "$(uname -m)" = "aarch64"

WORKDIR /src
COPY workspace/common/ ./common/
COPY workspace/proto/ ./proto/
COPY workspace/sdk/ ./sdk/
COPY workspace/sidecar/ ./sidecar/

RUN cmake -S sidecar -B sidecar-build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=ON \
    && cmake --build sidecar-build --parallel "${BUILD_JOBS}" \
    && ctest --test-dir sidecar-build --output-on-failure \
      --exclude-regex '^ucx-transport-test$' \
    && cmake -S sdk -B sdk-build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=ON \
    && cmake --build sdk-build --parallel "${BUILD_JOBS}" \
    && ctest --test-dir sdk-build --output-on-failure

RUN mkdir -p /out \
    && g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic \
      -I/src/sdk/include \
      -I/src/common \
      sidecar/tools/cascade_worker.cpp \
      -L/src/sdk-build -Wl,-rpath,/app \
      -luestcradar_sdk \
      -o /out/cascade-worker \
    && cp sidecar-build/sidecar /out/sidecar \
    && cp sidecar-build/network/ucx-network-benchmark \
      /out/ucx-network-benchmark \
    && cp sidecar-build/network/sidecar-network-benchmark \
      /out/sidecar-network-benchmark

FROM --platform=${SIDECAR_PLATFORM} ${RUNTIME_BASE} AS runtime

LABEL io.uestcradar.contract="sidecar/v2"

RUN test "$(uname -m)" = "aarch64"
WORKDIR /app
COPY --from=build /out/sidecar /app/sidecar
ENTRYPOINT ["/app/sidecar"]

FROM --platform=${SIDECAR_PLATFORM} ${RUNTIME_BASE} AS cascade-worker

RUN test "$(uname -m)" = "aarch64"
WORKDIR /app
COPY --from=build /out/cascade-worker /app/cascade-worker
COPY --from=build /src/sdk-build/libuestcradar_sdk.so* /app/
ENTRYPOINT ["/app/cascade-worker"]

FROM --platform=${SIDECAR_PLATFORM} ${RUNTIME_BASE} AS network-benchmark

RUN test "$(uname -m)" = "aarch64"
WORKDIR /app
COPY --from=build /out/ucx-network-benchmark /app/ucx-network-benchmark
ENTRYPOINT ["/app/ucx-network-benchmark"]

FROM --platform=${SIDECAR_PLATFORM} ${RUNTIME_BASE} AS forwarder-benchmark

RUN test "$(uname -m)" = "aarch64"
WORKDIR /app
COPY --from=build /out/sidecar-network-benchmark /app/network-benchmark
ENTRYPOINT ["/app/network-benchmark"]
