cmake_minimum_required(VERSION 3.16)
project(mexce VERSION 1.0.1 LANGUAGES CXX)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(ENABLE_COVERAGE "Build with coverage instrumentation" OFF)
option(MEXCE_ENABLE_PROTECTED_EXPRESSIONS "Build protected-expression targets" OFF)
option(MEXCE_BUILD_ISSUER_TOOLS "Build the protected-expression issuer tool" OFF)

if(MEXCE_BUILD_ISSUER_TOOLS AND NOT MEXCE_ENABLE_PROTECTED_EXPRESSIONS)
    message(FATAL_ERROR
        "MEXCE_BUILD_ISSUER_TOOLS requires MEXCE_ENABLE_PROTECTED_EXPRESSIONS")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(BUILD_TESTING AND ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-O0 -g)

        # cold_start_concurrency_test drives instrumented mexce code on 16
        # threads at once. The default non-atomic counter update loses
        # increments under that contention: gcov then flow-solves a negative
        # count for some derived arc and geninfo aborts, while llvm-cov
        # silently under-reports instead. Both legs instrument that test, so
        # both need the flag. Use atomic, not prefer-atomic, which falls back
        # to the racy update.
        add_compile_options(-fprofile-update=atomic)

        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options(--coverage)
            add_link_options(--coverage)
        else()
            add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
            add_link_options(-fprofile-instr-generate)
        endif()
    endif()
endif()

if(MSVC)
    set(MEXCE_OPTIMIZATION_FLAGS /O2)
    set(MEXCE_WARNING_FLAGS /W4 /permissive- /bigobj)
else()
    set(MEXCE_OPTIMIZATION_FLAGS -O3 -DNDEBUG)
    set(MEXCE_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

add_library(mexce INTERFACE)
add_library(mexce::mexce ALIAS mexce)
set_target_properties(mexce PROPERTIES EXPORT_NAME mexce)
target_include_directories(mexce INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(MEXCE_ENABLE_PROTECTED_EXPRESSIONS)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "MEXCE protected expressions support x64 targets only")
    endif()

    include(cmake/mexce-sodium.cmake)
    mexce_find_sodium(mexce::_sodium)

    add_library(mexce_protected INTERFACE)
    add_library(mexce::protected ALIAS mexce_protected)
    set_target_properties(mexce_protected PROPERTIES EXPORT_NAME protected)
    target_link_libraries(mexce_protected INTERFACE mexce mexce::_sodium)
endif()

if(MEXCE_BUILD_ISSUER_TOOLS)
    add_library(mexce_protect_lib STATIC tools/mexce_protect_lib.cpp)
    target_include_directories(mexce_protect_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    target_link_libraries(mexce_protect_lib PUBLIC mexce_protected)
    target_compile_options(mexce_protect_lib PRIVATE ${MEXCE_WARNING_FLAGS})
    if(WIN32)
        target_link_libraries(mexce_protect_lib PRIVATE Advapi32)
    endif()

    add_executable(mexce_protect tools/mexce_protect.cpp)
    target_link_libraries(mexce_protect PRIVATE mexce_protect_lib)
    target_compile_options(mexce_protect PRIVATE ${MEXCE_WARNING_FLAGS})
endif()

if(BUILD_TESTING)
    add_executable(benchmark test/benchmark.cpp)
    target_include_directories(benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    find_package(OpenMP REQUIRED)
    target_link_libraries(benchmark PRIVATE OpenMP::OpenMP_CXX)
    if(NOT ENABLE_COVERAGE)
        target_compile_options(benchmark PRIVATE ${MEXCE_OPTIMIZATION_FLAGS})
    endif()
    target_compile_options(benchmark PRIVATE ${MEXCE_WARNING_FLAGS})

    add_executable(unit_tests test/unit_tests.cpp)
    target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(unit_tests PRIVATE ${MEXCE_WARNING_FLAGS})

    add_executable(ordinary_no_sodium_test test/ordinary_no_sodium_test.cpp)
    target_link_libraries(ordinary_no_sodium_test PRIVATE mexce)
    target_compile_options(ordinary_no_sodium_test PRIVATE ${MEXCE_WARNING_FLAGS})

    find_package(Threads REQUIRED)
    add_executable(cold_start_concurrency_test test/cold_start_concurrency_test.cpp)
    target_link_libraries(cold_start_concurrency_test PRIVATE mexce Threads::Threads)
    target_compile_options(cold_start_concurrency_test PRIVATE ${MEXCE_WARNING_FLAGS})

    if(MEXCE_ENABLE_PROTECTED_EXPRESSIONS)
        set(MEXCE_PROTECTED_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
        find_package(Python3 COMPONENTS Interpreter REQUIRED)

        add_executable(protected_codec_tests test/protected_codec_tests.cpp)
        target_link_libraries(protected_codec_tests PRIVATE mexce_protected)
        target_compile_definitions(protected_codec_tests PRIVATE
            MEXCE_PROTECTED_TESTING
            MEXCE_PROTECTED_FIXTURE_PATH="${MEXCE_PROTECTED_FIXTURE_DIR}/protected_format_1_0.bin"
            MEXCE_PROTECTED_FIXTURE_MANIFEST_PATH="${MEXCE_PROTECTED_FIXTURE_DIR}/protected_format_1_0.manifest.txt")
        target_compile_options(protected_codec_tests PRIVATE ${MEXCE_WARNING_FLAGS})

        add_executable(protected_fixture_generator EXCLUDE_FROM_ALL
            test/protected_fixture_generator.cpp)
        target_link_libraries(protected_fixture_generator PRIVATE mexce::_sodium)
        target_compile_options(protected_fixture_generator PRIVATE ${MEXCE_WARNING_FLAGS})

        add_executable(protected_sodium_init_failure_test
            test/protected_sodium_init_failure_test.cpp)
        target_link_libraries(protected_sodium_init_failure_test PRIVATE mexce_protected)
        target_compile_definitions(protected_sodium_init_failure_test PRIVATE
            MEXCE_PROTECTED_TESTING)
        target_compile_options(protected_sodium_init_failure_test PRIVATE ${MEXCE_WARNING_FLAGS})

        add_executable(protected_runtime_tests test/protected_runtime_tests.cpp)
        target_link_libraries(protected_runtime_tests PRIVATE mexce_protected)
        target_compile_definitions(protected_runtime_tests PRIVATE
            MEXCE_PROTECTED_TESTING
            MEXCE_PROTECTED_FIXTURE_PATH="${MEXCE_PROTECTED_FIXTURE_DIR}/protected_format_1_0.bin")
        target_compile_options(protected_runtime_tests PRIVATE ${MEXCE_WARNING_FLAGS})

        add_executable(mixed_tu_test
            test/mixed_tu_test.cpp
            test/mixed_tu_ordinary.cpp
            test/mixed_tu_protected.cpp)
        target_link_libraries(mixed_tu_test PRIVATE mexce_protected)
        target_compile_definitions(mixed_tu_test PRIVATE
            MEXCE_PROTECTED_FIXTURE_PATH="${MEXCE_PROTECTED_FIXTURE_DIR}/protected_format_1_0.bin")
        target_compile_options(mixed_tu_test PRIVATE ${MEXCE_WARNING_FLAGS})

        add_executable(protected_benchmark test/protected_benchmark.cpp)
        target_link_libraries(protected_benchmark PRIVATE mexce_protected)
        if(WIN32)
            target_link_libraries(protected_benchmark PRIVATE Psapi)
        endif()
        if(NOT ENABLE_COVERAGE)
            target_compile_options(protected_benchmark PRIVATE ${MEXCE_OPTIMIZATION_FLAGS})
        endif()
        target_compile_options(protected_benchmark PRIVATE ${MEXCE_WARNING_FLAGS})

        add_executable(protected_decoder_fuzz_smoke test/protected_decoder_fuzz.cpp)
        target_link_libraries(protected_decoder_fuzz_smoke PRIVATE mexce_protected)
        target_compile_definitions(protected_decoder_fuzz_smoke PRIVATE
            MEXCE_FUZZ_SMOKE_MAIN
            MEXCE_PROTECTED_FIXTURE_PATH="${MEXCE_PROTECTED_FIXTURE_DIR}/protected_format_1_0.bin")
        target_compile_options(protected_decoder_fuzz_smoke PRIVATE ${MEXCE_WARNING_FLAGS})

        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND UNIX)
            add_executable(protected_decoder_fuzz EXCLUDE_FROM_ALL
                test/protected_decoder_fuzz.cpp)
            target_link_libraries(protected_decoder_fuzz PRIVATE mexce_protected)
            target_compile_options(protected_decoder_fuzz PRIVATE
                -fsanitize=fuzzer,address,undefined
                -fno-omit-frame-pointer)
            target_link_options(protected_decoder_fuzz PRIVATE
                -fsanitize=fuzzer,address,undefined)
        endif()

        if(MEXCE_BUILD_ISSUER_TOOLS)
            add_executable(mexce_protect_tests test/mexce_protect_tests.cpp)
            target_link_libraries(mexce_protect_tests PRIVATE mexce_protect_lib)
            target_compile_options(mexce_protect_tests PRIVATE ${MEXCE_WARNING_FLAGS})
        endif()
    endif()

    set(MEXCE_BENCHMARK_COMPILER "${CMAKE_CXX_COMPILER_ID}")
    if(CMAKE_CXX_COMPILER_VERSION)
        string(APPEND MEXCE_BENCHMARK_COMPILER " ${CMAKE_CXX_COMPILER_VERSION}")
    endif()

    if(ENABLE_COVERAGE)
        set(MEXCE_BENCHMARK_FLAGS "-O0 -g")
    else()
        list(JOIN MEXCE_OPTIMIZATION_FLAGS " " MEXCE_BENCHMARK_FLAGS)
    endif()
    list(JOIN MEXCE_WARNING_FLAGS " " MEXCE_WARNING_FLAGS_STR)
    string(APPEND MEXCE_BENCHMARK_FLAGS " ${MEXCE_WARNING_FLAGS_STR}")

    foreach(variable MEXCE_BENCHMARK_COMPILER MEXCE_BENCHMARK_FLAGS)
        string(REPLACE "\\" "\\\\" ${variable} "${${variable}}")
        string(REPLACE "\"" "\\\"" ${variable} "${${variable}}")
    endforeach()

    target_compile_definitions(benchmark PRIVATE
        BENCHMARK_COMPILER="${MEXCE_BENCHMARK_COMPILER}"
        BENCHMARK_COMPILER_FLAGS="${MEXCE_BENCHMARK_FLAGS}")
    if(MEXCE_ENABLE_PROTECTED_EXPRESSIONS)
        target_compile_definitions(protected_benchmark PRIVATE
            BENCHMARK_COMPILER="${MEXCE_BENCHMARK_COMPILER}"
            BENCHMARK_COMPILER_FLAGS="${MEXCE_BENCHMARK_FLAGS}")
    endif()

    add_test(NAME mexce_unit_tests COMMAND unit_tests)
    add_test(NAME mexce_ordinary_no_sodium COMMAND ordinary_no_sodium_test)
    add_test(NAME mexce_cold_start_concurrency COMMAND cold_start_concurrency_test)
    if(MEXCE_ENABLE_PROTECTED_EXPRESSIONS)
        add_test(NAME mexce_protected_codec_tests COMMAND protected_codec_tests)
        add_test(NAME mexce_protected_sodium_init_failure
            COMMAND protected_sodium_init_failure_test)
        add_test(NAME mexce_protected_runtime COMMAND protected_runtime_tests)
        add_test(NAME mexce_mixed_translation_units COMMAND mixed_tu_test)
        add_test(NAME mexce_protected_decoder_fuzz_smoke
            COMMAND protected_decoder_fuzz_smoke)
        add_test(NAME mexce_protected_benchmark
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/analysis/run_protected_benchmarks.py
                $<TARGET_FILE:protected_benchmark>
                ${CMAKE_CURRENT_BINARY_DIR}/protected_benchmark_ctest.json
                --repeats 1
                --resource-repeats 1
                --diagnostic-only)
        if(MEXCE_BUILD_ISSUER_TOOLS)
            add_test(NAME mexce_protect_utility COMMAND mexce_protect_tests)
            add_test(NAME mexce_protect_cli
                COMMAND
                    ${CMAKE_COMMAND}
                    "-DMEXCE_PROTECT_EXECUTABLE=$<TARGET_FILE:mexce_protect>"
                    "-DMEXCE_PROTECT_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/mexce_protect_cli_test"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/test/mexce_protect_cli_test.cmake)
        endif()
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
endif()

set(mexce_install_targets mexce)
if(MEXCE_ENABLE_PROTECTED_EXPRESSIONS)
    list(APPEND mexce_install_targets mexce_protected)
endif()
install(TARGETS ${mexce_install_targets} EXPORT mexce_targets)
if(MEXCE_BUILD_ISSUER_TOOLS)
    install(TARGETS mexce_protect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(EXPORT mexce_targets
    FILE mexce-targets.cmake
    NAMESPACE mexce::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mexce)

install(FILES mexce.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(MEXCE_ENABLE_PROTECTED_EXPRESSIONS)
    install(FILES
        mexce_protected.h
        mexce_protected_encoder.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES cmake/mexce-sodium.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mexce)
    install(FILES THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/mexce)
endif()
install(FILES LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/mexce)

set(MEXCE_CONFIG_HAS_PROTECTED ${MEXCE_ENABLE_PROTECTED_EXPRESSIONS})
configure_package_config_file(
    cmake/mexce-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mexce-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mexce)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mexce-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mexce-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mexce-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mexce)
