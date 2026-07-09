cmake_minimum_required(VERSION 3.14)
project(uestc-cfar-plotter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    include
    ../sdk/include
)

add_library(cfar_plotter MODULE
    src/algorithm.cpp
)

set_target_properties(cfar_plotter PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)
