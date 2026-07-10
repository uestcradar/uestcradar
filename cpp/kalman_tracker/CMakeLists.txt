cmake_minimum_required(VERSION 3.14)
project(uestc-kalman-tracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    include
    ../sdk/include
)

add_library(kalman_tracker MODULE
    src/algorithm.cpp
)

set_target_properties(kalman_tracker PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)
