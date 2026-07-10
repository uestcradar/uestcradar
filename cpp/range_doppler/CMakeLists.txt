cmake_minimum_required(VERSION 3.14)
project(uestc-range-doppler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    include
    ../sdk/include
)

add_library(range_doppler MODULE
    src/algorithm.cpp
)

set_target_properties(range_doppler PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)

# 🟢 编译静态测试沙盒可执行文件
add_executable(qa_range_doppler_block
    src/algorithm.cpp
    test/qa_range_doppler_block.cpp
)

