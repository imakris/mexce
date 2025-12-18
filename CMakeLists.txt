cmake_minimum_required(VERSION 3.16)
project(mexce_benchmark LANGUAGES CXX)

option(ENABLE_COVERAGE "Build with coverage instrumentation" OFF)

if(ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O0 -g)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      add_compile_options(--coverage)
      add_link_options(--coverage)
    else()
      add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
      add_link_options(-fprofile-instr-generate)
    endif()
  endif()
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(benchmark test/benchmark.cpp)
target_include_directories(benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(OpenMP REQUIRED)
target_link_libraries(benchmark PRIVATE OpenMP::OpenMP_CXX)

# Define optimization flags explicitly - these are what matter for performance
# This ensures flags are unambiguous and consistently reported
if (MSVC)
    set(MEXCE_OPTIMIZATION_FLAGS /O2)
    set(MEXCE_WARNING_FLAGS /W4 /permissive- /bigobj)
else()
    set(MEXCE_OPTIMIZATION_FLAGS -O3 -DNDEBUG)
    set(MEXCE_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

# Apply optimization flags only when coverage is disabled
if(NOT ENABLE_COVERAGE)
    target_compile_options(benchmark PRIVATE ${MEXCE_OPTIMIZATION_FLAGS})
endif()
target_compile_options(benchmark PRIVATE ${MEXCE_WARNING_FLAGS})

add_executable(unit_tests test/unit_tests.cpp)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(unit_tests PRIVATE ${MEXCE_WARNING_FLAGS})

# Capture compiler info for benchmark output
set(MEXCE_BENCHMARK_COMPILER "${CMAKE_CXX_COMPILER_ID}")
if (CMAKE_CXX_COMPILER_VERSION)
    string(APPEND MEXCE_BENCHMARK_COMPILER " ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# Build the flags string from our explicitly defined flags
# This ensures what's reported matches exactly what's applied
if(ENABLE_COVERAGE)
    # Coverage mode uses -O0 -g instead of optimization flags
    set(MEXCE_BENCHMARK_FLAGS "-O0 -g")
else()
    list(JOIN MEXCE_OPTIMIZATION_FLAGS " " MEXCE_BENCHMARK_FLAGS)
endif()

# Append warning flags
list(JOIN MEXCE_WARNING_FLAGS " " MEXCE_WARNING_FLAGS_STR)
string(APPEND MEXCE_BENCHMARK_FLAGS " ${MEXCE_WARNING_FLAGS_STR}")

# Escape special characters for C++ string literals
foreach(_var MEXCE_BENCHMARK_COMPILER MEXCE_BENCHMARK_FLAGS)
    string(REPLACE "\\" "\\\\" ${_var} "${${_var}}")
    string(REPLACE "\"" "\\\"" ${_var} "${${_var}}")
endforeach()

target_compile_definitions(benchmark PRIVATE
    BENCHMARK_COMPILER="${MEXCE_BENCHMARK_COMPILER}"
    BENCHMARK_COMPILER_FLAGS="${MEXCE_BENCHMARK_FLAGS}"
)

enable_testing()
add_test(NAME mexce_unit_tests COMMAND unit_tests)
add_test(NAME benchmark_quick
         COMMAND benchmark 5 ${CMAKE_BINARY_DIR}/benchmark_quick_results.txt)
add_test(NAME benchmark_output_first
         COMMAND benchmark ${CMAKE_BINARY_DIR}/benchmark_output_first.txt 5)

add_custom_target(run_benchmarks
    COMMAND ${CMAKE_COMMAND} -E echo "Running benchmark with 100 iterations"
    COMMAND $<TARGET_FILE:benchmark> 100 ${CMAKE_BINARY_DIR}/benchmark_results.txt
    COMMENT "Executing mexce benchmarks"
    USES_TERMINAL)
