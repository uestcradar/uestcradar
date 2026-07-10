cmake_minimum_required(VERSION 3.14)
project(uestc-ifft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    include
    ../sdk/include
)

add_library(cycore_ifft_plugin MODULE
    src/ifft_block.cpp
)

set_target_properties(cycore_ifft_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)
