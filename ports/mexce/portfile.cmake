# Pin product source before the overlay-only release metadata commit.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO imakris/mexce
    REF c19b917a5f4bcd72964a74509cc24e6d2bc12159
    SHA512 3c67cc6cf0649ce5caf8f60ab9cf164b273543fbf46a30bf25eb6cf460ac6a29199edfb11e4050481cd896d745f8a53202d5fb06516d057a22659a5674c14292
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
