cmake_minimum_required(VERSION 3.16)

project(cycomm_network LANGUAGES CXX)

include(CTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UCX REQUIRED IMPORTED_TARGET ucx)

add_library(cycomm_network STATIC ucx_transport.cpp)
add_library(cycomm::network ALIAS cycomm_network)
target_compile_features(cycomm_network PUBLIC cxx_std_20)
target_include_directories(cycomm_network PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cycomm_network PUBLIC PkgConfig::UCX)
target_compile_options(cycomm_network PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ucx-network-benchmark benchmark.cpp)
target_link_libraries(ucx-network-benchmark PRIVATE cycomm::network)
target_compile_options(ucx-network-benchmark PRIVATE -Wall -Wextra -Wpedantic)

add_executable(sidecar-network-benchmark ../tools/network_benchmark.cpp)
target_include_directories(
    sidecar-network-benchmark PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}/../../common")
target_link_libraries(sidecar-network-benchmark PRIVATE cycomm::network)
target_compile_options(
    sidecar-network-benchmark PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(ucx-transport-test transport_test.cpp)
    target_link_libraries(ucx-transport-test PRIVATE cycomm::network)
    target_compile_options(ucx-transport-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ucx-transport-test COMMAND ucx-transport-test)
    set_tests_properties(ucx-transport-test PROPERTIES TIMEOUT 30)
endif()
