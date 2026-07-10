cmake_minimum_required(VERSION 3.10)
project(my-algorithm-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 💡 指定包含脚手架内部相对路径下的 SDK 及本地头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/sdk/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 声明编译为 MODULE (运行时通过 dlopen 加载的插件 .so)
add_library(my_plugin MODULE
    src/algorithm_block.cpp
)

set_target_properties(my_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)
