function(mexce_find_sodium adapter_target)
    set(provider_target "")

    if(TARGET "${adapter_target}")
        return()
    endif()

    find_package(libsodium 1.0.22 EXACT CONFIG QUIET)
    if(TARGET libsodium::libsodium)
        set(provider_target libsodium::libsodium)
    else()
        find_package(unofficial-sodium 1.0.22 EXACT CONFIG QUIET)
        if(TARGET unofficial-sodium::sodium)
            set(provider_target unofficial-sodium::sodium)
        else()
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(MEXCE_SODIUM QUIET IMPORTED_TARGET libsodium=1.0.22)
            endif()
            if(TARGET PkgConfig::MEXCE_SODIUM)
                set(provider_target PkgConfig::MEXCE_SODIUM)
            endif()
        endif()
    endif()

    if(NOT provider_target)
        message(FATAL_ERROR
            "MEXCE protected expressions require libsodium 1.0.22 via "
            "libsodium::libsodium, unofficial-sodium::sodium, or pkg-config")
    endif()

    add_library("${adapter_target}" INTERFACE IMPORTED)
    set_property(TARGET "${adapter_target}" PROPERTY
        INTERFACE_LINK_LIBRARIES "${provider_target}")
endfunction()
