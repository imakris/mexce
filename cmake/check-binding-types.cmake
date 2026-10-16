function(mexce_check_binding_types)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/test/binding_compile_test.cpp")
    set(probe_flags
        "-DCMAKE_CXX_STANDARD=11"
        "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
        "-DCMAKE_FASTBUILD_CAPTURE_SYSTEM_ENV=OFF")
    try_compile(supported
        "${CMAKE_CURRENT_BINARY_DIR}/binding-supported"
        "${source}"
        CMAKE_FLAGS ${probe_flags}
        COMPILE_DEFINITIONS -DNDEBUG
        OUTPUT_VARIABLE supported_output)
    if(NOT supported)
        message(FATAL_ERROR "Supported bindings failed to compile:\n${supported_output}")
    endif()

    try_compile(unsupported
        "${CMAKE_CURRENT_BINARY_DIR}/binding-unsupported"
        "${source}"
        CMAKE_FLAGS ${probe_flags}
        COMPILE_DEFINITIONS -DNDEBUG -DMEXCE_TEST_UNSUPPORTED_BINDING
        OUTPUT_VARIABLE unsupported_output)
    if(unsupported OR NOT unsupported_output MATCHES "Bindings support double")
        message(FATAL_ERROR
            "Unsupported binding did not produce the type constraint diagnostic:\n${unsupported_output}")
    endif()
endfunction()

mexce_check_binding_types()
