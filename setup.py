import os
import re
import pathlib
import platform
import subprocess

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from distutils.version import LooseVersion

class CMakeExtension(Extension):

    def __init__(self, name):
        # don't invoke the original build_ext for this special extension
        super().__init__(name, sources=[])


class CMakeBuild(build_ext):

    def run(self):
        try:
            out = subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError("CMake must be installed to build the following extensions: " +
                               ", ".join(e.name for e in self.extensions))

        if platform.system() == "Windows":
            cmake_version = LooseVersion(re.search(r'version\s*([\d.]+)', out.decode()).group(1))
            if cmake_version < '3.1.0':
                raise RuntimeError("CMake >= 3.1.0 is required on Windows")

        for ext in self.extensions:
            self.build_extension(ext)
        super().run()

    def build_extension(self, ext):
        env = os.environ.copy()
        cwd = pathlib.Path().absolute()
        cfg = 'Debug' if self.debug else 'Release'

        # these dirs will be created in build_py, so if you don't have
        # any python sources to bundle, the dirs will be missing
        build_temp = pathlib.Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)

        extdir = pathlib.Path(self.get_ext_fullpath(ext.name))
        extdir.parent.mkdir(parents=True, exist_ok=True)

        # exit()
        cmake_args = [
            '-DCMAKE_BUILD_TYPE=' + cfg,
            '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + str(extdir.parent.absolute()),

            '-DBUILD_SHARED_LIBS=OFF',

            # XXX Uncomment the following lines to get lighter OBJ only build for development
            # '-DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=FALSE',
            # '-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=FALSE',
            # '-DASSIMP_BUILD_OBJ_IMPORTER=TRUE'
        ]

        build_args = [
            '--config', cfg
        ]

        os.chdir(str(build_temp))
        self.spawn(['cmake', str(cwd)] + cmake_args)
        if not self.dry_run:
            self.spawn(['cmake', '--build', '.'] + build_args)
        os.chdir(str(cwd))


# The directory containing this file
HERE = pathlib.Path(__file__).parent
README = (HERE/"README.md").read_text()


setup(
    name="assimp_py",
    version="1.0.2",
    long_description=README,
    long_description_content_type="text/markdown",
    description="Minimal Python Bindings for ASSIMP Library using C-API",
    author="Ian Ichung'wah",
    author_email="karanjaichungwa@gmail.com",
    license="MIT",
    classifiers=[
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9"
    ],
    ext_modules=[CMakeExtension("assimp_py")],
    cmdclass={
        'build_ext': CMakeBuild,
    }
)
