cmake_minimum_required(VERSION 3.16)
project(uestcradar_algorithm LANGUAGES CXX)

include(CTest)
find_package(cycomm_sdk CONFIG REQUIRED)

option(AGILE_SIGNAL_USE_OPENMP "Enable original algorithm OpenMP" ON)

add_executable(algorithm main.cpp echo_process.cpp)
target_link_libraries(algorithm PRIVATE cycomm::sdk)
target_compile_features(algorithm PRIVATE cxx_std_20)
target_compile_options(algorithm PRIVATE -Wall -Wextra -Wpedantic)

if(AGILE_SIGNAL_USE_OPENMP)
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(algorithm PRIVATE OpenMP::OpenMP_CXX)
        target_compile_definitions(
            algorithm PRIVATE AGILE_SIGNAL_USE_OPENMP=1)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(algorithm-test algorithm_test.cpp)
    target_link_libraries(algorithm-test PRIVATE cycomm::sdk)
    target_compile_features(algorithm-test PRIVATE cxx_std_20)
    target_compile_options(
        algorithm-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME algorithm-test COMMAND algorithm-test)

    set(CPI0_TEST_DATA_DIR "" CACHE PATH "Optional authoritative CPI0 test data")
    if(CPI0_TEST_DATA_DIR)
        add_executable(
            cpi-adapter-test
            cpi_adapter_test.cpp
            echo_process.cpp)
        target_link_libraries(cpi-adapter-test PRIVATE cycomm::sdk)
        target_include_directories(
            cpi-adapter-test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../signalsource/src")
        target_compile_features(cpi-adapter-test PRIVATE cxx_std_20)
        target_compile_options(
            cpi-adapter-test PRIVATE -Wall -Wextra -Wpedantic)
        if(OpenMP_CXX_FOUND)
            target_link_libraries(
                cpi-adapter-test PRIVATE OpenMP::OpenMP_CXX)
            target_compile_definitions(
                cpi-adapter-test PRIVATE AGILE_SIGNAL_USE_OPENMP=1)
        endif()
        add_test(
            NAME cpi-adapter-real-data
            COMMAND cpi-adapter-test "${CPI0_TEST_DATA_DIR}")
        set_tests_properties(
            cpi-adapter-real-data PROPERTIES TIMEOUT 600)
    endif()
endif()
