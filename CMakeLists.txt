cmake_minimum_required(VERSION 3.10)
project(math-parser-benchmark)

# Set C++ standard to C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the benchmark executable
add_executable(benchmark benchmark.cpp)

# Custom target to run the benchmark
add_custom_target(
    run_benchmark
    COMMAND ${CMAKE_COMMAND} -E echo "Running benchmark with bench_expr.txt..."
    COMMAND $<TARGET_FILE:benchmark> ${CMAKE_SOURCE_DIR}/bench_expr.txt > bench_expr_results.txt
    COMMAND ${CMAKE_COMMAND} -E echo "Running benchmark with bench_expr_all.txt..."
    COMMAND $<TARGET_FILE:benchmark> ${CMAKE_SOURCE_DIR}/bench_expr_all.txt > bench_expr_all_results.txt
    DEPENDS benchmark
    COMMENT "Running benchmarks"
)
