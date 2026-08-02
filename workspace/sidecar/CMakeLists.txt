cmake_minimum_required(VERSION 3.16)

project(cycomm_sidecar_core LANGUAGES CXX)

include(CTest)
find_package(Threads REQUIRED)
set(Protobuf_USE_STATIC_LIBS ON)
find_package(Protobuf QUIET)

set(UESTCRADAR_RINGBUF_BUILD_BENCHMARK ON)
add_subdirectory(
    ../common/ringbuf
    "${CMAKE_CURRENT_BINARY_DIR}/ringbuf")
add_subdirectory(network)

add_library(cycomm_forwarder STATIC forwarder/forwarder.cpp)
add_library(cycomm::forwarder ALIAS cycomm_forwarder)
target_compile_features(cycomm_forwarder PUBLIC cxx_std_20)
target_include_directories(
    cycomm_forwarder PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    cycomm_forwarder
    PUBLIC uestcradar::ringbuf cycomm::network Threads::Threads)
target_compile_options(
    cycomm_forwarder PRIVATE -Wall -Wextra -Wpedantic)

if(Protobuf_FOUND)
    if(TARGET protobuf::libprotobuf-lite)
        set(SIDECAR_PROTOBUF_TARGET protobuf::libprotobuf-lite)
    elseif(TARGET protobuf::libprotobuf)
        set(SIDECAR_PROTOBUF_TARGET protobuf::libprotobuf)
    else()
        message(
            FATAL_ERROR
            "Protobuf was found, but neither libprotobuf-lite nor libprotobuf is available")
    endif()

    protobuf_generate_cpp(
        TELEMETRY_PROTO_SOURCES
        TELEMETRY_PROTO_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/../proto/telemetry.proto")

    add_library(
        cycomm_telemetry STATIC
        telemetry/telemetry.cpp
        ${TELEMETRY_PROTO_SOURCES})
    target_compile_features(cycomm_telemetry PUBLIC cxx_std_20)
    target_include_directories(
        cycomm_telemetry
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(
        cycomm_telemetry
        PRIVATE ${SIDECAR_PROTOBUF_TARGET} Threads::Threads)
    target_compile_options(
        cycomm_telemetry PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(sidecar main.cpp)
    target_compile_features(sidecar PRIVATE cxx_std_20)
    target_link_libraries(
        sidecar
        PRIVATE
            cycomm::forwarder
            cycomm_telemetry
            uestcradar::ringbuf
            cycomm::network
            Threads::Threads)
    target_compile_options(sidecar PRIVATE -Wall -Wextra -Wpedantic)
else()
    message(
        STATUS
        "Protobuf development files not found; sidecar daemon target disabled")
endif()

if(BUILD_TESTING)
    add_executable(
        forwarder-test forwarder/forwarder_test.cpp)
    target_link_libraries(
        forwarder-test PRIVATE cycomm::forwarder)
    target_compile_options(
        forwarder-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME forwarder-test COMMAND forwarder-test)
    set_tests_properties(forwarder-test PROPERTIES TIMEOUT 30)

    add_executable(
        forwarder-contract-test
        forwarder/forwarder_contract_test.cpp)
    target_link_libraries(
        forwarder-contract-test PRIVATE cycomm::forwarder)
    target_compile_options(
        forwarder-contract-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(
        NAME forwarder-contract-test
        COMMAND forwarder-contract-test)
    set_tests_properties(
        forwarder-contract-test PROPERTIES TIMEOUT 30)

    add_executable(
        leg-lifecycle-test
        forwarder/leg_lifecycle_test.cpp)
    target_link_libraries(
        leg-lifecycle-test PRIVATE cycomm::forwarder)
    target_compile_options(
        leg-lifecycle-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(
        NAME leg-lifecycle-test
        COMMAND leg-lifecycle-test)
endif()
