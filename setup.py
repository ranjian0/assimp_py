import os
import sys
import pathlib
import platform
import subprocess
import multiprocessing

from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext

PYTHON_VERSION = f"{sys.version_info.major}.{sys.version_info.minor}"

class CMakeExtension(Extension):

    def __init__(self, name, sourcedir=""):
        # don't invoke the original build_ext for this special extension
        super().__init__(name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):

    def run(self):
        try:
            out = subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError("CMake must be installed to build the following extensions: " +
                               ", ".join(e.name for e in self.extensions))

        for ext in self.extensions:
            self.build_extension(ext)
        super().run()

    def build_extension(self, ext):
        cfg = 'Debug' if self.debug else 'Release'

        # these dirs will be created in build_py, so if you don't have
        # any python sources to bundle, the dirs will be missing
        build_temp = pathlib.Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)

        extdir = pathlib.Path(self.get_ext_fullpath(ext.name))
        extdir.parent.mkdir(parents=True, exist_ok=True)
        print(extdir)

        cmake_args = [
            '-DCMAKE_BUILD_TYPE=' + cfg,
            '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + str(extdir.parent.absolute()),

            # Tells cmake which python version to use for this build
            '-DREQUESTED_PYTHON_VERSION=' + PYTHON_VERSION,

            # This is the only reliable way I could find to get extension name on all platforms
            # Used within CMakeLists.txt
            '-DEXTENSION_NAME='+ str(extdir.name),

            # Assimp Flags
            '-DASSIMP_BUILD_ZLIB=ON',
            '-DBUILD_SHARED_LIBS=OFF',
            '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF',
            '-DASSIMP_BUILD_TESTS=OFF',
            '-DASSIMP_WARNINGS_AS_ERRORS=OFF',
            '-DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=FALSE',

            # XXX Uncomment the following lines to get lighter OBJ only build for development
            # '-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=FALSE',
            # '-DASSIMP_BUILD_OBJ_IMPORTER=TRUE'
        ]


        # We can handle some platform-specific settings at our discretion
        if platform.system() == 'Windows':
            cmake_args += [
                # These options are likely to be needed under Windows
                '-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=TRUE',
                '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_{}={}'.format(cfg.upper(), str(extdir.parent.absolute())),
            ]

        # Multicor build for dev
        build_args = ['--config', cfg, '-j', str(multiprocessing.cpu_count())]
        self.spawn(['cmake', '-S', '.', '-B', str(build_temp)] + cmake_args)
        if not self.dry_run:
            self.spawn(['cmake', '--build', str(build_temp)] + build_args)

setup(
    packages=['assimp_py'],
    package_dir={'': 'src'},
    ext_modules=[CMakeExtension("assimp_py.assimp_py", sourcedir="src/assimp")],
    cmdclass={
        'build_ext': CMakeBuild,
    },
    exclude_package_data={
        'assimp_py': [
            '*.c',
        ]
    }
)
