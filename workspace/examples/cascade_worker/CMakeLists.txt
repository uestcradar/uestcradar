cmake_minimum_required(VERSION 3.20)
project(uestcradar_cascade_worker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 支持预装在 algo-base 基础镜像中的 cycomm_sdk 或 uestcradar_sdk
find_package(cycomm_sdk QUIET)

add_executable(cascade-worker src/main.cpp)

if(TARGET cycomm::sdk)
    target_link_libraries(cascade-worker PRIVATE cycomm::sdk)
else()
    # 显式包含头文件与动态库路径
    target_include_directories(cascade-worker PRIVATE /app/sdk/include /src/sdk/include)
    target_link_directories(cascade-worker PRIVATE /app /app/lib /src/sdk-build)
    target_link_libraries(cascade-worker PRIVATE uestcradar_sdk)
endif()
