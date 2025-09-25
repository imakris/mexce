cmake_minimum_required(VERSION 3.16)
project(mexce_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(benchmark test/benchmark.cpp)
target_include_directories(benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(OpenMP REQUIRED)
target_link_libraries(benchmark PRIVATE OpenMP::OpenMP_CXX)

if (MSVC)
    target_compile_options(benchmark PRIVATE /W4 /permissive- /bigobj)
else()
    target_compile_options(benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME benchmark_quick
         COMMAND benchmark 5 ${CMAKE_BINARY_DIR}/benchmark_quick_results.txt)
add_test(NAME benchmark_output_first
         COMMAND benchmark ${CMAKE_BINARY_DIR}/benchmark_output_first.txt 5)

add_custom_target(run_benchmarks
    COMMAND ${CMAKE_COMMAND} -E echo "Running benchmark with 100 iterations"
    COMMAND $<TARGET_FILE:benchmark> 100 ${CMAKE_BINARY_DIR}/benchmark_results.txt
    COMMENT "Executing mexce benchmarks"
    USES_TERMINAL)
