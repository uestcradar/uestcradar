# syntax=docker/dockerfile:1
# Must be a fixed, verified SDK image containing Input<RawFrame>::try_read().
ARG ALGO_BASE
FROM ${ALGO_BASE}
WORKDIR /app
COPY CMakeLists.txt ./CMakeLists.txt
COPY src/ ./src/
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build --parallel \
    && cp build/signalsink /app/signalsink \
    && rm -rf build src CMakeLists.txt
ENTRYPOINT ["/app/signalsink"]
LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.component="signalsink" \
      io.uestcradar.roles="sink" \
      io.uestcradar.input="any" \
      io.uestcradar.output="none"
