cmake_minimum_required(VERSION 3.16)
project(uestcradar_signalsource LANGUAGES CXX)

include(CTest)

find_package(cycomm_sdk CONFIG REQUIRED)

add_executable(signalsource main.cpp)
target_link_libraries(signalsource PRIVATE cycomm::sdk)
target_compile_features(signalsource PRIVATE cxx_std_20)
target_compile_options(signalsource PRIVATE -Wall -Wextra -Wpedantic)

add_executable(frame-sink sink.cpp)
target_link_libraries(frame-sink PRIVATE cycomm::sdk)
target_compile_features(frame-sink PRIVATE cxx_std_20)
target_compile_options(frame-sink PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(waveform-test waveform_test.cpp)
    target_link_libraries(waveform-test PRIVATE cycomm::sdk)
    target_compile_features(waveform-test PRIVATE cxx_std_20)
    target_compile_options(waveform-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME signalsource-waveform COMMAND waveform-test)
endif()
