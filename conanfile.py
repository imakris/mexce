from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain
from conan.tools.layout import basic_layout


class MexceConan(ConanFile):
    name = "mexce"
    version = "1.0.1"
    license = "BSD-2-Clause"
    homepage = "https://github.com/imakris/mexce"
    url = "https://github.com/conan-io/conan-center-index"
    description = "Header-only JIT compiler for scalar mathematical expressions."
    topics = ("jit", "math", "expression-parser", "header-only")
    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"with_protected": [True, False]}
    default_options = {"with_protected": False}
    exports_sources = (
        "CMakeLists.txt",
        "LICENSE.txt",
        "THIRD_PARTY_NOTICES.md",
        "cmake/*",
        "mexce.h",
        "mexce_protected.h",
        "mexce_protected_encoder.h",
    )
    no_copy_source = True

    def requirements(self):
        if self.options.with_protected:
            self.requires("libsodium/1.0.22")

    def layout(self):
        basic_layout(self)

    def validate(self):
        allowed_architectures = {"x86", "x86_64"}
        if str(self.settings.arch) not in allowed_architectures:
            raise ConanInvalidConfiguration(
                "mexce only supports x86 and x86_64 architectures")
        if self.options.with_protected and str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration(
                "mexce protected expressions support x86_64 packages only")

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_TESTING"] = False
        toolchain.variables["MEXCE_ENABLE_PROTECTED_EXPRESSIONS"] = bool(
            self.options.with_protected)
        toolchain.generate()
        CMakeDeps(self).generate()

    def build(self):
        CMake(self).configure()

    def package(self):
        CMake(self).install()

    def package_id(self):
        self.info.settings.clear()

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs = ["lib/cmake/mexce"]
