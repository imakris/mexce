cmake_minimum_required(VERSION 3.16)
project(mexce_test_package LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(mexce CONFIG REQUIRED)

add_executable(mexce_ordinary_consumer test_package.cpp)
target_link_libraries(mexce_ordinary_consumer PRIVATE mexce::mexce)

if(TARGET mexce::protected)
    add_executable(mexce_protected_consumer protected_test_package.cpp)
    target_link_libraries(mexce_protected_consumer PRIVATE mexce::protected)
endif()
