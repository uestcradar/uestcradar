cmake_minimum_required(VERSION 3.16)
project(uestcradar_cascade_worker LANGUAGES CXX)

find_package(cycomm_sdk REQUIRED)

add_executable(cascade-worker src/main.cpp)
target_link_libraries(cascade-worker PRIVATE cycomm::sdk)
