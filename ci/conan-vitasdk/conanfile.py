import os

from conans import ConanFile, tools


class VitasdkToolchainConan(ConanFile):
    name = "vitasdk-toolchain"
    lib_version = "08"
    package_version = ""
    exports_sources = "cmake-toolchain.patch"
    version = "%s%s" % (lib_version, package_version)
    settings = "os", "arch"

    def source(self):
        tools.download("https://github.com/blastrock/autobuilds/releases/download/master-linux-v8/vitasdk-x86_64-linux-gnu-2018-10-30_19-22-35.tar.bz2", filename="vitasdk.tar.bz2")
        tools.untargz("vitasdk.tar.bz2")

        tools.patch(base_path="vitasdk", patch_file="cmake-toolchain.patch", strip=1)

        additional_libs = ["libvita2d", "libpng", "libjpeg-turbo", "zlib"]
        for lib in additional_libs:
            lib = "{}.tar.xz".format(lib)
            tools.download("http://dl.vitasdk.org/{}".format(lib), filename=lib)
            tools.untargz(lib, os.path.join("vitasdk", "arm-vita-eabi"))

    def package(self):
        self.copy(pattern="*", src="vitasdk")

    def package_info(self):
        self.env_info.VITASDK = os.path.join(self.package_folder)
        self.env_info.PATH.append(os.path.join(self.package_folder, "bin"))
        self.env_info.CONAN_CMAKE_TOOLCHAIN_FILE = os.path.join(self.package_folder, "share", "vita.toolchain.cmake")
