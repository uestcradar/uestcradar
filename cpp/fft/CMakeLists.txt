cmake_minimum_required(VERSION 3.14)
project(uestc-fft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    include
    ../sdk/include
)

add_library(cycore_fft_plugin MODULE
    src/fft_block.cpp
)

set_target_properties(cycore_fft_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)
