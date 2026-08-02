cmake_minimum_required(VERSION 3.16)
project(uestcradar_signalsource LANGUAGES CXX)

include(CTest)

find_package(cycomm_sdk CONFIG REQUIRED)

add_executable(signalsource main.cpp)
target_link_libraries(signalsource PRIVATE cycomm::sdk)
target_compile_features(signalsource PRIVATE cxx_std_20)
target_compile_options(signalsource PRIVATE -Wall -Wextra -Wpedantic)

add_executable(pulse-source pulse_source.cpp)
target_link_libraries(pulse-source PRIVATE cycomm::sdk)
target_compile_features(pulse-source PRIVATE cxx_std_20)
target_compile_options(pulse-source PRIVATE -Wall -Wextra -Wpedantic)

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

    add_executable(cpi-data-test cpi_data_test.cpp)
    target_link_libraries(cpi-data-test PRIVATE cycomm::sdk)
    target_compile_features(cpi-data-test PRIVATE cxx_std_20)
    target_compile_options(cpi-data-test PRIVATE -Wall -Wextra -Wpedantic)
    set(CPI0_TEST_DATA_DIR "" CACHE PATH "Optional authoritative CPI0 test data")
    if(CPI0_TEST_DATA_DIR)
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
            COMMAND cpi-data-test "${CPI0_TEST_DATA_DIR}")
        add_test(
            NAME signalsource-cpi-ring
            COMMAND cpi-ring-test "${CPI0_TEST_DATA_DIR}")
        add_test(
            NAME signalsource-cpi-sidecar
            COMMAND cpi-sidecar-test "${CPI0_TEST_DATA_DIR}")
        set_tests_properties(
            signalsource-cpi-sidecar PROPERTIES TIMEOUT 30)
    endif()
endif()
