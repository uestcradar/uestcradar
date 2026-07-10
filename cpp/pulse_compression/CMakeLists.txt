cmake_minimum_required(VERSION 3.14)
project(uestc-pulse-compression LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    include
    ../sdk/include
)

add_library(pulse_compression MODULE
    src/algorithm.cpp
)

set_target_properties(pulse_compression PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)

# 🟢 编译静态测试沙盒可执行文件
add_executable(qa_pulse_compression_block
    src/algorithm.cpp
    test/qa_pulse_compression_block.cpp
)

