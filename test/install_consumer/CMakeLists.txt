cmake_minimum_required(VERSION 3.16)
project(mexce_install_consumer LANGUAGES CXX)

option(MEXCE_CONSUMER_PROTECTED "Build the protected installed consumer" OFF)
set(MEXCE_FORBIDDEN_INCLUDE_ROOT "" CACHE PATH
    "Source root that must not appear in installed target include paths")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MEXCE_POISON_SODIUM_PROVIDER)
    set(provider_target bogus::normal)
endif()

find_package(mexce CONFIG REQUIRED)

get_target_property(mexce_includes mexce::mexce INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(mexce_links mexce::mexce INTERFACE_LINK_LIBRARIES)
if(mexce_links MATCHES "[Ss]odium")
    message(FATAL_ERROR "mexce::mexce exposes a sodium link item: ${mexce_links}")
endif()

if(MEXCE_FORBIDDEN_INCLUDE_ROOT)
    file(TO_CMAKE_PATH "${MEXCE_FORBIDDEN_INCLUDE_ROOT}" forbidden_include_root)
    string(TOLOWER "${forbidden_include_root}" forbidden_include_root)
    string(TOLOWER "${mexce_includes}" normalized_mexce_includes)
    string(FIND "${normalized_mexce_includes}" "${forbidden_include_root}" source_root_index)
    if(NOT source_root_index EQUAL -1)
        message(FATAL_ERROR
            "Installed mexce::mexce resolves into the source tree: ${mexce_includes}")
    endif()
endif()

add_executable(mexce_ordinary_consumer ordinary.cpp)
target_link_libraries(mexce_ordinary_consumer PRIVATE mexce::mexce)

if(MEXCE_CONSUMER_PROTECTED)
    if(NOT TARGET mexce::protected)
        message(FATAL_ERROR "The installed package does not provide mexce::protected")
    endif()
    get_target_property(protected_links mexce::protected INTERFACE_LINK_LIBRARIES)
    if(protected_links MATCHES "libsodium::|unofficial-sodium::|PkgConfig::")
        message(FATAL_ERROR
            "mexce::protected leaks a provider target: ${protected_links}")
    endif()

    add_executable(mexce_protected_consumer protected.cpp)
    target_link_libraries(mexce_protected_consumer PRIVATE mexce::protected)

    add_executable(mexce_protected_file_consumer
        ${CMAKE_CURRENT_LIST_DIR}/../../protected_example.cpp)
    target_link_libraries(mexce_protected_file_consumer PRIVATE mexce::protected)
elseif(TARGET mexce::protected)
    message(FATAL_ERROR "The ordinary installed package unexpectedly provides mexce::protected")
endif()
