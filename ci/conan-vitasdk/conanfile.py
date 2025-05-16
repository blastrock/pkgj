import os

from conan import ConanFile
from conan.tools import files


class VitasdkToolchainConan(ConanFile):
    name = "vitasdk-toolchain"
    lib_version = "2.527"
    package_version = ""
    exports_sources = "cmake-toolchain.patch"
    version = "%s%s" % (lib_version, package_version)
    settings = "os", "arch"
    user = "blastrock"
    channel = "pkgj"

    def source(self):
        files.download(
            self,
            "https://github.com/vitasdk/autobuilds/releases/download/master-linux-v2.527/vitasdk-x86_64-linux-gnu-2024-08-09_11-28-39.tar.bz2",
            filename="vitasdk.tar.bz2",
        )
        files.unzip(self, "vitasdk.tar.bz2")

        additional_libs = [
            "libvita2d",
            "libpng",
            "libjpeg-turbo",
            "taihen",
        ]
        for lib in additional_libs:
            lib = "{}.tar.xz".format(lib)
            files.download(self, "https://github.com/vitasdk/packages/releases/download/master/{}".format(lib), filename=lib)
            files.unzip(self, lib, os.path.join("vitasdk", "arm-vita-eabi"))

    def package(self):
        files.copy(self, pattern="*", src="vitasdk", dst=self.package_folder)

    def package_info(self):
        self.buildenv_info.define_path("VITASDK", os.path.join(self.package_folder))
        self.buildenv_info.append_path("PATH", os.path.join(self.package_folder, "bin"))
        self.buildenv_info.define_path(
            "CONAN_CMAKE_TOOLCHAIN_FILE",
            os.path.join(self.package_folder, "share", "vita.toolchain.cmake"),
        )

        self.conf_info.define(
            "tools.cmake.cmaketoolchain:user_toolchain",
            [os.path.join(self.package_folder, "share/vita.toolchain.cmake")],
        )
