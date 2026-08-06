# syntax=docker/dockerfile:1
ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest
ARG CPI_DATA_IMAGE=registry.chengyistudio.com/cxx/worker:signalsource-latest@sha256:c6833658a15367f4c4b115fa6391f563af26c7a04853519379f424da72c5feb9

FROM ${CPI_DATA_IMAGE} AS cpi-data
FROM ${ALGO_BASE}

WORKDIR /app
COPY src/ ./src/
COPY --from=cpi-data /data/ /data/

# 只编译离线 CPI IQFrame 数据源。
RUN cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build --parallel \
    && cp build/signalsource ./signalsource \
    && rm -rf src build

ENTRYPOINT ["/app/signalsource"]

LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="source" \
      io.uestcradar.input="none" \
      io.uestcradar.output="1:3"
