from conans import ConanFile, CMake, tools

required_conan_version = ">=1.33.0"

class TheoraPlayerConan(ConanFile):
    name = "theora-player"
    url = "https://github.com/firefalcom/conan-theora-player"
    homepage = "https://github.com/firefalcom/theora-player"
    description = "Embeddable Theora Player"
    topics = "video", "embeddable", "player"
    license = "MIT"

    settings = "os", "arch", "compiler", "build_type"
    options = {}
    default_options = {}

    generators = ["cmake"]

    exports_sources = "CMakeLists.txt", "../src/*", "../include/*"

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def requirements(self):
        self.requires("ogg/1.3.5")
        self.requires("theora/1.1.1")
        self.requires("vorbis/1.3.7")

    def package(self):
        self.copy("*.h", dst="include")
        self.copy("*.a", dst="lib", keep_path=False)
        self.copy("*.lib", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["theora-player"]
