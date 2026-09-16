ARG ALGO_BASE=registry.chengyistudio.com/cxx/algo-base:GFKD

FROM ${ALGO_BASE}

# The published builder contains sample sources and a CMake cache.
# Rebuild the supplied project from a clean source/build directory.
RUN rm -rf /src /build

WORKDIR /src
COPY CMakeLists.txt ./
COPY src/ ./src/

RUN cmake -S /src -B /build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=ON \
        -DBUILD_INFRA=OFF \
    && cmake --build /build --parallel \
        --target qt5-algorithm rd-algorithm-test \
    && ctest --test-dir /build --output-on-failure

WORKDIR /app
RUN cp /build/qt5-algorithm /app/qt5-algorithm

ENTRYPOINT ["/app/qt5-algorithm"]

LABEL io.uestcradar.contract="worker/v2" \
      io.uestcradar.roles="operator" \
      io.uestcradar.input="2:2" \
      io.uestcradar.output="3:2"
