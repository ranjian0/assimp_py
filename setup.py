import os
import sys
import pathlib
import platform
import sysconfig
import subprocess
import configparser
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
        # NOTE: deliberately python-version-independent - cibuildwheel builds
        # each python in sequence and make skips the ~500 unchanged assimp
        # objects on the 2nd/3rd builds instead of recompiling them
        build_temp = pathlib.Path('build') / 'cmake'
        build_temp.mkdir(parents=True, exist_ok=True)

        extdir = pathlib.Path(self.get_ext_fullpath(ext.name))
        extdir.parent.mkdir(parents=True, exist_ok=True)
        print(extdir)

        cmake_args = [
            '-DCMAKE_BUILD_TYPE=' + cfg,
            '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + str(extdir.parent.absolute()),

            # Tells cmake which python version to use for this build
            '-DREQUESTED_PYTHON_VERSION=' + PYTHON_VERSION,

            # pin the interpreter for FindPython: the shared cmake build dir
            # caches the previous python build's detection, and cibuildwheel
            # removes that build env between builds - without the pin cmake
            # re-searches and can find the container's ancient system python
            '-DPython_EXECUTABLE=' + sys.executable,

            # This is the only reliable way I could find to get extension name on all platforms
            # Used within CMakeLists.txt
            '-DEXTENSION_NAME='+ str(extdir.name),
        ]


        # We can handle some platform-specific settings at our discretion
        if platform.system() == 'Windows':
            cmake_args += [
                # These options are likely to be needed under Windows
                '-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=TRUE',
                '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_{}={}'.format(cfg.upper(), str(extdir.parent.absolute())),
            ]

            # cibuildwheel cross-compiles ARM64 wheels on amd64 runners;
            # select the ARM64 platform for the Visual Studio generator
            # (EXT_SUFFIX in extdir.name carries the target platform).
            # FindPython cannot be used for the ARM64 target (x64 host
            # interpreter), so pass the arch-independent include dir directly.
            if 'win_arm64' in extdir.name:
                cmake_args += [
                    '-A', 'ARM64',
                    '-DPython_INCLUDE_DIRS=' + sysconfig.get_paths()['include'],
                ]

                # pyconfig.h pragma-references python3XX.lib under MSVC; the
                # ARM64 import libs are published via cibuildwheel's
                # DIST_EXTRA_CONFIG ([build_ext] library_dirs)
                dist_cfg = os.environ.get('DIST_EXTRA_CONFIG')
                if dist_cfg and os.path.exists(dist_cfg):
                    parser = configparser.ConfigParser()
                    parser.read(dist_cfg)
                    if parser.has_option('build_ext', 'library_dirs'):
                        cmake_args += [
                            '-DPython_LIBRARY_DIRS=' + parser.get('build_ext', 'library_dirs'),
                        ]

        # Multicor build for dev
        build_args = ['--config', cfg, '-j', str(multiprocessing.cpu_count())]
        self.spawn(['cmake', '-S', '.', '-B', str(build_temp)] + cmake_args)
        if not self.dry_run:
            self.spawn(['cmake', '--build', str(build_temp)] + build_args)

setup(
    packages=['assimp_py'],
    package_dir={'': 'src'},
    ext_modules=[CMakeExtension("assimp_py.assimp_py")],
    cmdclass={
        'build_ext': CMakeBuild,
    },
)
