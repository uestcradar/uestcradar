cmake_minimum_required(VERSION 3.16)

project(cycomm_sdk VERSION 6.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
include(CheckIPOSupported)

# The SDK relies on the monorepo's shared RingBuffer implementation.
set(CYCOMM_RINGBUF_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/ringbuf"
    CACHE PATH "Path to the SDK's vendored RingBuffer source")
if(NOT EXISTS "${CYCOMM_RINGBUF_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "CYCOMM_RINGBUF_SOURCE_DIR must contain the vendored RingBuffer source")
endif()
add_subdirectory(
    "${CYCOMM_RINGBUF_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/ringbuf"
)

file(GLOB CYCOMM_CONTRACT_SCHEMAS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/*.json")
set(CYCOMM_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(STRINGS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/contract_catalog.def"
    CYCOMM_CONTRACT_CATALOG_LINES)
set(CYCOMM_GENERATED_CONTRACT_SOURCES)
foreach(catalog_line IN LISTS CYCOMM_CONTRACT_CATALOG_LINES)
    if(catalog_line MATCHES
       "^UESTCRADAR_CONTRACT\\([ \t]*([a-zA-Z0-9_]+)[ \t]*,")
        list(APPEND CYCOMM_GENERATED_CONTRACT_SOURCES
            "${CYCOMM_GENERATED_DIR}/contract_${CMAKE_MATCH_1}.generated.cpp")
    endif()
endforeach()
if(NOT CYCOMM_GENERATED_CONTRACT_SOURCES)
    message(FATAL_ERROR "contract_catalog.def contains no contracts")
endif()
set(CYCOMM_GENERATED_CONTRACTS
    "${CYCOMM_GENERATED_DIR}/contract_traits.generated.hpp"
    "${CYCOMM_GENERATED_DIR}/preview_contracts.generated.hpp"
    "${CYCOMM_GENERATED_DIR}/contracts.manifest.json"
    "${CYCOMM_GENERATED_DIR}/contracts.generated.go"
    "${CYCOMM_GENERATED_DIR}/contracts.generated.ts"
    ${CYCOMM_GENERATED_CONTRACT_SOURCES}
)
set(CYCOMM_CONTRACT_GENERATION_STAMP
    "${CYCOMM_GENERATED_DIR}/contracts.generated.stamp")
set_source_files_properties(
    ${CYCOMM_GENERATED_CONTRACT_SOURCES}
    "${CYCOMM_GENERATED_DIR}/contract_traits.generated.hpp"
    PROPERTIES GENERATED TRUE)

add_executable(contract-codegen tools/contract_codegen.cpp)
target_compile_features(contract-codegen PRIVATE cxx_std_17)
target_compile_options(contract-codegen PRIVATE -Wall -Wextra -Wpedantic)

add_custom_command(
    OUTPUT "${CYCOMM_CONTRACT_GENERATION_STAMP}"
    BYPRODUCTS ${CYCOMM_GENERATED_CONTRACTS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CYCOMM_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:contract-codegen>
        "${CMAKE_CURRENT_SOURCE_DIR}/include/contract_catalog.def"
        "${CMAKE_CURRENT_SOURCE_DIR}/contracts"
        "${CYCOMM_GENERATED_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch
        "${CYCOMM_CONTRACT_GENERATION_STAMP}"
    DEPENDS
        contract-codegen
        "${CMAKE_CURRENT_SOURCE_DIR}/include/contract_catalog.def"
        ${CYCOMM_CONTRACT_SCHEMAS}
    VERBATIM
)
add_custom_target(generate-contracts
    DEPENDS "${CYCOMM_CONTRACT_GENERATION_STAMP}")

add_library(uestcradar_sdk SHARED
    src/sdk.cpp
    "${CYCOMM_GENERATED_DIR}/contract_traits.generated.hpp"
    ${CYCOMM_GENERATED_CONTRACT_SOURCES}
)
add_library(cycomm::sdk ALIAS uestcradar_sdk)
add_dependencies(uestcradar_sdk generate-contracts)

set_target_properties(uestcradar_sdk PROPERTIES
    OUTPUT_NAME uestcradar_sdk
    EXPORT_NAME sdk
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
check_ipo_supported(RESULT CYCOMM_IPO_SUPPORTED OUTPUT CYCOMM_IPO_ERROR)
if(CYCOMM_IPO_SUPPORTED)
    set_property(TARGET uestcradar_sdk PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

target_compile_features(uestcradar_sdk PUBLIC cxx_std_20)
target_compile_definitions(uestcradar_sdk PRIVATE CYCOMM_SDK_BUILD)
target_link_libraries(uestcradar_sdk PRIVATE uestcradar::ringbuf)
target_include_directories(uestcradar_sdk
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CYCOMM_GENERATED_DIR}"
)

if(BUILD_TESTING)
    add_executable(sdk-test src/sdk_test.cpp)
    target_link_libraries(
        sdk-test PRIVATE uestcradar_sdk uestcradar::ringbuf)
    target_include_directories(
        sdk-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_options(
        sdk-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME sdk-test COMMAND sdk-test)
    add_executable(sdk-interface-test src/sdk_interface_test.cpp)
    target_link_libraries(sdk-interface-test PRIVATE uestcradar_sdk)
    target_compile_options(
        sdk-interface-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME sdk-interface-test COMMAND sdk-interface-test)
    add_executable(sdk-benchmark src/sdk_benchmark.cpp)
    target_link_libraries(
        sdk-benchmark PRIVATE uestcradar_sdk uestcradar::ringbuf)
    target_include_directories(
        sdk-benchmark PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_options(
        sdk-benchmark PRIVATE -Wall -Wextra -Wpedantic)
    add_test(
        NAME contract-codegen-test
        COMMAND ${CMAKE_COMMAND}
            -DCODEGEN=$<TARGET_FILE:contract-codegen>
            -DSDK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/contract-codegen-test
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ContractCodegenTest.cmake)
endif()

if(UNIX AND NOT APPLE)
    set_property(TARGET uestcradar_sdk APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/exports.map")
    target_link_options(uestcradar_sdk PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/exports.map"
    )
endif()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cycomm_sdkConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    cmake/cycomm_sdkConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cycomm_sdkConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cycomm_sdk"
)

install(TARGETS uestcradar_sdk
    EXPORT cycomm_sdkTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES
    include/sdk.h
    include/data.h
    include/contract_catalog.def
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES
    "${CYCOMM_GENERATED_DIR}/contracts.manifest.json"
    "${CYCOMM_GENERATED_DIR}/preview_contracts.generated.hpp"
    "${CYCOMM_GENERATED_DIR}/contracts.generated.go"
    "${CYCOMM_GENERATED_DIR}/contracts.generated.ts"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/cycomm_sdk/contracts")
install(EXPORT cycomm_sdkTargets
    FILE cycomm_sdkTargets.cmake
    NAMESPACE cycomm::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cycomm_sdk"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cycomm_sdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cycomm_sdkConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cycomm_sdk"
)
