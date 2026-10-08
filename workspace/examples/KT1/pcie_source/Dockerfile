# syntax=docker/dockerfile:1
# Must contain RawIQFrame 4:1 and Output<T>::try_create; pin candidate SDK digest for release.
ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest
FROM ${ALGO_BASE} AS build
WORKDIR /app
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY pcie/ ./pcie/
COPY tests/ ./tests/
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    && cmake --build build --parallel 4 \
    && ctest --test-dir build --output-on-failure

FROM ${ALGO_BASE}
WORKDIR /app
COPY --from=build /app/build/pcie_source /app/pcie_source
COPY pcie_config/config_sync_4.txt pcie_config/config_drp_4g8.txt pcie_config/log.json /app/pcie_config/
ENTRYPOINT ["/app/pcie_source"]
LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="source" \
      io.uestcradar.input="none" \
      io.uestcradar.output="4:1"
