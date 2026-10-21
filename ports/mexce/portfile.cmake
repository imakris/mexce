# Pin product source before the overlay-only release metadata commit.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO imakris/mexce
    REF fa427873de640fac0a014708ad7d221258bf8671
    SHA512 0c869a1aea6eca701a1e16c95982d500094bed9112f6a133b9e8bde91a4b5e1d5710e4e8c90b5a9688e966765a8d631e3c23890ea969b8653d074900c2f9c293
    HEAD_REF master
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        protected MEXCE_ENABLE_PROTECTED_EXPRESSIONS
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTING=OFF
        -DMEXCE_BUILD_ISSUER_TOOLS=OFF
        ${FEATURE_OPTIONS}
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/mexce)

if("protected" IN_LIST FEATURES)
    file(INSTALL "${SOURCE_PATH}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
endif()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug"
    "${CURRENT_PACKAGES_DIR}/lib"
    "${CURRENT_PACKAGES_DIR}/share/licenses")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
