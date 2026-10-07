# syntax=docker/dockerfile:1
ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest
ARG CPI_DATA_IMAGE=registry.chengyistudio.com/cxx/worker:signalsource-latest@sha256:c6833658a15367f4c4b115fa6391f563af26c7a04853519379f424da72c5feb9

FROM ${CPI_DATA_IMAGE} AS cpi-data
RUN set -eu; for n in 0 1 2 3 4 5 6 7 8 9; do \
      mkdir -p /templates/CPI$n; \
      for file in metadata.json pulse_time.txt pulse_phase.txt pulse_freq.txt wd0.txt; do \
        cp /data/CPI$n/$file /templates/CPI$n/; \
      done; \
    done

FROM ${ALGO_BASE} AS build
WORKDIR /app
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY pcie/ ./pcie/
COPY tests/ ./tests/
COPY --from=cpi-data /templates/ /data/
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    && cmake --build build --parallel 4 \
    && ctest --test-dir build --output-on-failure \
    && ./build/cpi_templates_test /data

FROM ${ALGO_BASE}
WORKDIR /app
COPY --from=build /app/build/pcie_source /app/pcie_source
COPY --from=cpi-data /templates/ /data/
COPY pcie_config/config_sync_4.txt pcie_config/config_drp_4g8.txt pcie_config/log.json /app/pcie_config/
ENTRYPOINT ["/app/pcie_source"]
LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="source" \
      io.uestcradar.input="none" \
      io.uestcradar.output="1:3"
