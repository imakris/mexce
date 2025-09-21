cmake_minimum_required(VERSION 3.16)
project(mexce_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(benchmark benchmark.cpp)
target_include_directories(benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
    target_compile_options(benchmark PRIVATE /W4 /permissive-)
else()
    target_compile_options(benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME bench_expr
         COMMAND benchmark ${CMAKE_SOURCE_DIR}/bench_expr.txt 100)
add_test(NAME bench_expr_all
         COMMAND benchmark ${CMAKE_SOURCE_DIR}/bench_expr_all.txt 100)

add_custom_target(run_benchmarks
    COMMAND ${CMAKE_COMMAND} -E echo "Running bench_expr.txt with 100 iterations"
    COMMAND $<TARGET_FILE:benchmark> ${CMAKE_SOURCE_DIR}/bench_expr.txt 100 ${CMAKE_BINARY_DIR}/bench_expr_results.txt
    COMMAND ${CMAKE_COMMAND} -E echo "Running bench_expr_all.txt with 100 iterations"
    COMMAND $<TARGET_FILE:benchmark> ${CMAKE_SOURCE_DIR}/bench_expr_all.txt 100 ${CMAKE_BINARY_DIR}/bench_expr_all_results.txt
    DEPENDS benchmark
    COMMENT "Executing mexce benchmarks"
    USES_TERMINAL)
