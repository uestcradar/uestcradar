cmake_minimum_required(VERSION 3.16)

project(cycomm_sdk VERSION 5.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

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

add_library(uestcradar_sdk SHARED
    src/sdk.cpp
)
add_library(cycomm::sdk ALIAS uestcradar_sdk)

set_target_properties(uestcradar_sdk PROPERTIES
    OUTPUT_NAME uestcradar_sdk
    EXPORT_NAME sdk
)

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
endif()

if(UNIX AND NOT APPLE)
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
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
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
