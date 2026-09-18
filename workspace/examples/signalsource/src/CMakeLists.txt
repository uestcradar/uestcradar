cmake_minimum_required(VERSION 3.16)
project(uestcradar_signalsource LANGUAGES CXX)

include(CTest)

find_package(cycomm_sdk CONFIG REQUIRED)

add_executable(signalsource main.cpp)
target_link_libraries(signalsource PRIVATE cycomm::sdk)
target_compile_features(signalsource PRIVATE cxx_std_20)
target_compile_options(signalsource PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(cpi-data-test cpi_data_test.cpp)
    target_link_libraries(cpi-data-test PRIVATE cycomm::sdk)
    target_compile_features(cpi-data-test PRIVATE cxx_std_20)
    target_compile_options(cpi-data-test PRIVATE -Wall -Wextra -Wpedantic)
    set(CPI_TEST_DATA_ROOT "" CACHE PATH "Optional authoritative CPI0-CPI9 root")
    set(CPI0_TEST_DATA_DIR "" CACHE PATH "Deprecated path to authoritative CPI0")
    if(NOT CPI_TEST_DATA_ROOT AND CPI0_TEST_DATA_DIR)
        get_filename_component(_cpi0_name "${CPI0_TEST_DATA_DIR}" NAME)
        if(_cpi0_name STREQUAL "CPI0")
            get_filename_component(
                CPI_TEST_DATA_ROOT "${CPI0_TEST_DATA_DIR}" DIRECTORY)
        else()
            set(CPI_TEST_DATA_ROOT "${CPI0_TEST_DATA_DIR}")
        endif()
    endif()
    if(CPI_TEST_DATA_ROOT)
        find_package(Threads REQUIRED)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(UCX REQUIRED IMPORTED_TARGET ucx)
        set(UESTCRADAR_RINGBUF_BUILD_BENCHMARK OFF)
        add_subdirectory(
            ../../../common/ringbuf
            "${CMAKE_CURRENT_BINARY_DIR}/ringbuf")
        add_executable(cpi-ring-test cpi_ring_test.cpp)
        target_link_libraries(
            cpi-ring-test PRIVATE cycomm::sdk uestcradar::ringbuf)
        target_include_directories(
            cpi-ring-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
        target_compile_features(cpi-ring-test PRIVATE cxx_std_20)
        target_compile_options(
            cpi-ring-test PRIVATE -Wall -Wextra -Wpedantic)
        add_executable(
            cpi-sidecar-test
            cpi_sidecar_test.cpp
            ../../../sidecar/forwarder/forwarder.cpp
            ../../../sidecar/network/ucx_transport.cpp)
        target_link_libraries(
            cpi-sidecar-test PRIVATE
            cycomm::sdk
            uestcradar::ringbuf
            PkgConfig::UCX
            Threads::Threads)
        target_include_directories(
            cpi-sidecar-test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../sidecar"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../common")
        target_compile_features(cpi-sidecar-test PRIVATE cxx_std_20)
        target_compile_options(
            cpi-sidecar-test PRIVATE -Wall -Wextra -Wpedantic)
        add_test(
            NAME signalsource-cpi-data
            COMMAND cpi-data-test "${CPI_TEST_DATA_ROOT}")
        add_test(
            NAME signalsource-cpi-ring
            COMMAND cpi-ring-test "${CPI_TEST_DATA_ROOT}")
        add_test(
            NAME signalsource-cpi-sidecar
            COMMAND cpi-sidecar-test "${CPI_TEST_DATA_ROOT}/CPI0")
        set_tests_properties(
            signalsource-cpi-sidecar PROPERTIES TIMEOUT 30)
    endif()
endif()
