cmake_minimum_required(VERSION 3.16)
project(uestcradar_signalsink LANGUAGES CXX)

include(CTest)
find_package(cycomm_sdk CONFIG REQUIRED)
find_package(Threads REQUIRED)

function(configure_sink_target target)
    target_link_libraries(${target} PRIVATE cycomm::sdk Threads::Threads)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3>)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        target_compile_options(${target} PRIVATE -march=armv8.2-a+simd)
    endif()
endfunction()

add_executable(signalsink src/main.cpp)
configure_sink_target(signalsink)

add_executable(pcsink-benchmark src/benchmark.cpp)
configure_sink_target(pcsink-benchmark)

if(BUILD_TESTING)
    add_executable(signalsink-test src/target_validator_test.cpp)
    configure_sink_target(signalsink-test)
    add_test(NAME signalsink-target-validation COMMAND signalsink-test)
endif()
