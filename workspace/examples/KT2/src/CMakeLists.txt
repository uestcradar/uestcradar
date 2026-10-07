cmake_minimum_required(VERSION 3.16)
project(uestcradar_pulsecompression LANGUAGES CXX)

include(CTest)
find_package(cycomm_sdk CONFIG REQUIRED)

function(configure_worker target)
    target_link_libraries(${target} PRIVATE cycomm::sdk)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

add_executable(pulsecompression main.cpp)
configure_worker(pulsecompression)

if(BUILD_TESTING)
    add_executable(pulsecompression-test pulsecompression_test.cpp)
    configure_worker(pulsecompression-test)
    add_test(NAME pulsecompression-test COMMAND pulsecompression-test)

    set(CPI_DATA_ROOT "" CACHE PATH "Optional authoritative CPI0-CPI9 root")
    if(CPI_DATA_ROOT)
        add_executable(reference-data-test reference_data_test.cpp)
        configure_worker(reference-data-test)
        add_test(
            NAME pulsecompression-reference-data
            COMMAND reference-data-test "${CPI_DATA_ROOT}")
    endif()
endif()
