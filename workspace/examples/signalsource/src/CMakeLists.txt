cmake_minimum_required(VERSION 3.16)
project(uestcradar_signalsource LANGUAGES CXX)

find_package(cycomm_sdk CONFIG REQUIRED)

add_executable(signalsource main.cpp)
target_link_libraries(signalsource PRIVATE cycomm::sdk)
target_compile_features(signalsource PRIVATE cxx_std_20)
target_compile_options(signalsource PRIVATE -Wall -Wextra -Wpedantic)

add_executable(frame-sink sink.cpp)
target_link_libraries(frame-sink PRIVATE cycomm::sdk)
target_compile_features(frame-sink PRIVATE cxx_std_20)
target_compile_options(frame-sink PRIVATE -Wall -Wextra -Wpedantic)
