cmake_minimum_required(VERSION 3.16)
project(mexce_benchmark LANGUAGES CXX)

option(ENABLE_COVERAGE "Build with coverage instrumentation" OFF)
option(MEXCE_ENABLE_PROTECTED_EXPRESSIONS "Build protected-expression targets" OFF)

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

add_library(mexce INTERFACE)
add_library(mexce::mexce ALIAS mexce)
target_include_directories(mexce INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(MEXCE_ENABLE_PROTECTED_EXPRESSIONS)
    set(MEXCE_SODIUM_TARGET "")
    find_package(libsodium 1.0.22 EXACT CONFIG QUIET)
    if(TARGET libsodium::libsodium)
        set(MEXCE_SODIUM_TARGET libsodium::libsodium)
    else()
        find_package(unofficial-sodium 1.0.22 EXACT CONFIG QUIET)
    endif()
    if(TARGET unofficial-sodium::sodium)
        set(MEXCE_SODIUM_TARGET unofficial-sodium::sodium)
    elseif(NOT MEXCE_SODIUM_TARGET)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(MEXCE_SODIUM QUIET IMPORTED_TARGET libsodium=1.0.22)
        endif()
        if(TARGET PkgConfig::MEXCE_SODIUM)
            set(MEXCE_SODIUM_TARGET PkgConfig::MEXCE_SODIUM)
        else()
            message(FATAL_ERROR "MEXCE protected expressions require libsodium 1.0.22")
        endif()
    endif()

    add_library(mexce_protected INTERFACE)
    add_library(mexce::protected ALIAS mexce_protected)
    target_link_libraries(mexce_protected INTERFACE mexce ${MEXCE_SODIUM_TARGET})

    set(MEXCE_PROTECTED_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_executable(protected_codec_tests test/protected_codec_tests.cpp)
    target_link_libraries(protected_codec_tests PRIVATE mexce_protected)
    target_compile_definitions(protected_codec_tests PRIVATE
        MEXCE_PROTECTED_TESTING
        MEXCE_PROTECTED_FIXTURE_PATH="${MEXCE_PROTECTED_FIXTURE_DIR}/protected_format_1_0.bin"
        MEXCE_PROTECTED_FIXTURE_MANIFEST_PATH="${MEXCE_PROTECTED_FIXTURE_DIR}/protected_format_1_0.manifest.txt")
    target_compile_options(protected_codec_tests PRIVATE ${MEXCE_WARNING_FLAGS})

    add_executable(protected_fixture_generator EXCLUDE_FROM_ALL
        test/protected_fixture_generator.cpp)
    target_link_libraries(protected_fixture_generator PRIVATE ${MEXCE_SODIUM_TARGET})
    target_compile_options(protected_fixture_generator PRIVATE ${MEXCE_WARNING_FLAGS})

    add_executable(protected_sodium_init_failure_test
        test/protected_sodium_init_failure_test.cpp)
    target_link_libraries(protected_sodium_init_failure_test PRIVATE mexce_protected)
    target_compile_definitions(protected_sodium_init_failure_test PRIVATE
        MEXCE_PROTECTED_TESTING)
    target_compile_options(protected_sodium_init_failure_test PRIVATE ${MEXCE_WARNING_FLAGS})
endif()

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
if(MEXCE_ENABLE_PROTECTED_EXPRESSIONS)
    add_test(NAME mexce_protected_codec_tests COMMAND protected_codec_tests)
    add_test(NAME mexce_protected_sodium_init_failure
        COMMAND protected_sodium_init_failure_test)
endif()
add_test(NAME benchmark_quick
         COMMAND benchmark 5 ${CMAKE_BINARY_DIR}/benchmark_quick_results.txt)
add_test(NAME benchmark_output_first
         COMMAND benchmark ${CMAKE_BINARY_DIR}/benchmark_output_first.txt 5)

add_custom_target(run_benchmarks
    COMMAND ${CMAKE_COMMAND} -E echo "Running benchmark with 100 iterations"
    COMMAND $<TARGET_FILE:benchmark> 100 ${CMAKE_BINARY_DIR}/benchmark_results.txt
    COMMENT "Executing mexce benchmarks"
    USES_TERMINAL)
