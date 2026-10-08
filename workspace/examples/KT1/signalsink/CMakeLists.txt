cmake_minimum_required(VERSION 3.16)
project(uestcradar_signalsink LANGUAGES CXX)
include(CTest)
find_package(cycomm_sdk CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(signalsink src/main.cpp src/control.cpp)
target_link_libraries(signalsink PRIVATE cycomm::sdk Threads::Threads)
target_compile_features(signalsink PRIVATE cxx_std_20)
target_compile_options(signalsink PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(signalsink-test tests/signalsink_test.cpp)
    target_link_libraries(signalsink-test PRIVATE Threads::Threads)
    target_compile_features(signalsink-test PRIVATE cxx_std_20)
    target_compile_options(signalsink-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME signalsink-test COMMAND signalsink-test)
    set_tests_properties(signalsink-test PROPERTIES TIMEOUT 20)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_subdirectory(../../../infra/common/ringbuf "${CMAKE_CURRENT_BINARY_DIR}/ringbuf")
    add_executable(signalsink-input-fixture tests/input_fixture.cpp)
    target_link_libraries(signalsink-input-fixture PRIVATE uestcradar::ringbuf)
    add_executable(signalsink-throughput tests/throughput.cpp)
    target_link_libraries(signalsink-throughput PRIVATE cycomm::sdk uestcradar::ringbuf Threads::Threads)
    add_test(NAME signalsink-integration COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py"
        "$<TARGET_FILE:signalsink>" "$<TARGET_FILE:signalsink-input-fixture>")
    set_tests_properties(signalsink-integration PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    add_test(NAME signalsink-throughput-smoke COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/throughput_smoke.py"
        "$<TARGET_FILE:signalsink>" "$<TARGET_FILE:signalsink-throughput>")
    set_tests_properties(signalsink-throughput-smoke PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()
