cmake_minimum_required(VERSION 3.14)
project(uestc-radar-algorithms LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 🟢 包含各个独立的 C++ 算子子项目
add_subdirectory(fft)
add_subdirectory(ifft)
add_subdirectory(pulse_compression)
add_subdirectory(range_doppler)
add_subdirectory(cfar_plotter)
add_subdirectory(kalman_tracker)
