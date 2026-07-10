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
