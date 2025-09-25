cmake_minimum_required(VERSION 3.16)
project(mexce_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(benchmark test/benchmark.cpp)
target_include_directories(benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(OpenMP REQUIRED)
target_link_libraries(benchmark PRIVATE OpenMP::OpenMP_CXX)

if (MSVC)
    set(MEXCE_BENCHMARK_LOCAL_FLAGS /W4 /permissive- /bigobj)
else()
    set(MEXCE_BENCHMARK_LOCAL_FLAGS -Wall -Wextra -Wpedantic)
endif()

target_compile_options(benchmark PRIVATE ${MEXCE_BENCHMARK_LOCAL_FLAGS})

set(MEXCE_BENCHMARK_COMPILER "${CMAKE_CXX_COMPILER_ID}")
if (CMAKE_CXX_COMPILER_VERSION)
    string(APPEND MEXCE_BENCHMARK_COMPILER " ${CMAKE_CXX_COMPILER_VERSION}")
endif()

set(MEXCE_BENCHMARK_FLAGS "${CMAKE_CXX_FLAGS}")
if (CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" MEXCE_BUILD_TYPE_UPPER)
    if (DEFINED CMAKE_CXX_FLAGS_${MEXCE_BUILD_TYPE_UPPER})
        set(MEXCE_BUILD_TYPE_FLAGS "${CMAKE_CXX_FLAGS_${MEXCE_BUILD_TYPE_UPPER}}")
        if (MEXCE_BUILD_TYPE_FLAGS)
            if (MEXCE_BENCHMARK_FLAGS)
                string(APPEND MEXCE_BENCHMARK_FLAGS " ")
            endif()
            string(APPEND MEXCE_BENCHMARK_FLAGS "${MEXCE_BUILD_TYPE_FLAGS}")
        endif()
    endif()
endif()

if (MEXCE_BENCHMARK_LOCAL_FLAGS)
    list(JOIN MEXCE_BENCHMARK_LOCAL_FLAGS " " MEXCE_BENCHMARK_LOCAL_FLAGS_STR)
    if (MEXCE_BENCHMARK_LOCAL_FLAGS_STR)
        if (MEXCE_BENCHMARK_FLAGS)
            string(APPEND MEXCE_BENCHMARK_FLAGS " ")
        endif()
        string(APPEND MEXCE_BENCHMARK_FLAGS "${MEXCE_BENCHMARK_LOCAL_FLAGS_STR}")
    endif()
endif()

string(STRIP "${MEXCE_BENCHMARK_FLAGS}" MEXCE_BENCHMARK_FLAGS)
if ("${MEXCE_BENCHMARK_FLAGS}" STREQUAL "")
    set(MEXCE_BENCHMARK_FLAGS "<none>")
endif()

foreach(_var MEXCE_BENCHMARK_COMPILER MEXCE_BENCHMARK_FLAGS)
    string(REPLACE "\\" "\\\\" ${_var} "${${_var}}")
    string(REPLACE "\"" "\\\"" ${_var} "${${_var}}")
endforeach()

target_compile_definitions(benchmark PRIVATE
    BENCHMARK_COMPILER="${MEXCE_BENCHMARK_COMPILER}"
    BENCHMARK_COMPILER_FLAGS="${MEXCE_BENCHMARK_FLAGS}"
)

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
