cmake_minimum_required(VERSION 3.14)
project(uestc-radar-algorithms LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 🟢 统一重定向所有动态库/插件和可执行测试产物至 build/lib 和 build/bin
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 🟢 包含各个独立的 C++ 算子子项目
add_subdirectory(fft)
add_subdirectory(ifft)
add_subdirectory(pulse_compression)
add_subdirectory(range_doppler)
add_subdirectory(cfar_plotter)
add_subdirectory(kalman_tracker)
