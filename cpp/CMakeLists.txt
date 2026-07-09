cmake_minimum_required(VERSION 3.15)
project(uestc_radar_kernels CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 🟢 依赖探测
find_package(FFTW3 QUIET)
if(NOT FFTW3_FOUND)
    message(STATUS "FFTW3 not found. CPU backend will fallback to standard DFT/FFT implementation.")
endif()

# 🟢 包含路径
include_directories(include)

# 🟢 编译子目录 (后续根据具体实现填充)
# add_subdirectory(src)
# add_subdirectory(tests)
