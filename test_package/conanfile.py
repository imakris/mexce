from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.build import can_run
import os


class MexceTestPackageConan(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            ordinary = os.path.join(
                self.cpp.build.bindirs[0], "mexce_ordinary_consumer")
            self.run(ordinary, env="conanrun")
            if self.dependencies["mexce"].options.with_protected:
                protected = os.path.join(
                    self.cpp.build.bindirs[0], "mexce_protected_consumer")
                self.run(protected, env="conanrun")
