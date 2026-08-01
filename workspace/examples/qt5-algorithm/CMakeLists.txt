cmake_minimum_required(VERSION 3.16)
project(qt5_algorithm_example LANGUAGES CXX)

include(CTest)
find_package(Qt5 COMPONENTS Core REQUIRED)
find_package(cycomm_sdk CONFIG REQUIRED)

add_executable(qt5-algorithm src/main.cpp)
target_link_libraries(qt5-algorithm PRIVATE Qt5::Core cycomm::sdk)
target_compile_features(qt5-algorithm PRIVATE cxx_std_20)
target_compile_options(qt5-algorithm PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(rd-algorithm-test src/rd_algorithm_test.cpp)
    target_link_libraries(rd-algorithm-test PRIVATE cycomm::sdk)
    target_compile_features(rd-algorithm-test PRIVATE cxx_std_20)
    target_compile_options(
        rd-algorithm-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rd-algorithm-test COMMAND rd-algorithm-test)
endif()
