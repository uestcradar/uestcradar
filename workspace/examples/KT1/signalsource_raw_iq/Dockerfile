# syntax=docker/dockerfile:1
ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:latest
FROM ${ALGO_BASE} AS build
WORKDIR /app
COPY CMakeLists.txt ./
COPY src/ ./src/
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    && cmake --build build --parallel 4 \
    && ctest --test-dir build --output-on-failure

FROM ${ALGO_BASE}
WORKDIR /app
COPY --from=build /app/build/signalsource-raw-iq /app/signalsource-raw-iq
ENTRYPOINT ["/app/signalsource-raw-iq"]
LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="source" \
      io.uestcradar.input="none" \
      io.uestcradar.output="4:1"
