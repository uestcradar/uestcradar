cmake_minimum_required(VERSION 3.16)
project(uestcradar_algorithm LANGUAGES CXX)

include(CTest)
find_package(cycomm_sdk CONFIG REQUIRED)

add_executable(algorithm main.cpp)
target_link_libraries(algorithm PRIVATE cycomm::sdk)
target_compile_features(algorithm PRIVATE cxx_std_20)
target_compile_options(algorithm PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(algorithm-test algorithm_test.cpp)
    target_link_libraries(algorithm-test PRIVATE cycomm::sdk)
    target_compile_features(algorithm-test PRIVATE cxx_std_20)
    target_compile_options(
        algorithm-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME algorithm-test COMMAND algorithm-test)
endif()
