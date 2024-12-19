from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps


class dwarf2cppRecipe(ConanFile):
    name = "dwarf2cpp"
    version = "0.0.1"
    package_type = "application"

    # Binary configuration
    settings = "os", "compiler", "build_type", "arch"
    default_options = {
        "llvm-core/*:targets": "X86",
        "llvm-core/*:with_ffi": False,
        "llvm-core/*:with_libedit": False,
        "llvm-core/*:with_zlib": False,
        "llvm-core/*:with_xml2": False,
        "llvm-core/*:with_z3": False,
    }

    # Sources are located in the same place as this recipe, copy them to the recipe
    exports_sources = "CMakeLists.txt", "src/*"

    def requirements(self):
        self.requires("argparse/3.1")
        self.requires("llvm-core/13.0.0")
        self.requires("spdlog/1.15.0")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
