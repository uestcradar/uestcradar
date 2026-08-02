cmake_minimum_required(VERSION 3.16)

project(uestcradar_ringbuf LANGUAGES CXX)

include(CTest)
# This target is the single implementation of the shared-memory RingBuffer
# ABI used by both the Sidecar and the worker SDK.
add_library(uestcradar_ringbuf STATIC ringbuf.cpp)
add_library(uestcradar::ringbuf ALIAS uestcradar_ringbuf)

set_target_properties(uestcradar_ringbuf PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_compile_features(uestcradar_ringbuf PUBLIC cxx_std_20)
target_include_directories(uestcradar_ringbuf
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(uestcradar_ringbuf PUBLIC rt)
endif()

set(_ringbuf_benchmark_default OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_ringbuf_benchmark_default ON)
endif()
option(
    UESTCRADAR_RINGBUF_BUILD_BENCHMARK
    "Build the fixed-slot RingBuffer benchmark"
    ${_ringbuf_benchmark_default})

if(UESTCRADAR_RINGBUF_BUILD_BENCHMARK)
    add_executable(ringbuf-benchmark benchmark.cpp)
    target_link_libraries(ringbuf-benchmark PRIVATE uestcradar_ringbuf)
    target_compile_options(
        ringbuf-benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(ringbuf-test ringbuf_test.cpp)
    target_link_libraries(ringbuf-test PRIVATE uestcradar_ringbuf)
    target_compile_options(
        ringbuf-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ringbuf-test COMMAND ringbuf-test)
endif()
