import os
import re
import sys
import pathlib
import platform
import subprocess

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from distutils.version import LooseVersion
from distutils.sysconfig import get_python_inc, get_config_var


class CMakeExtension(Extension):

    def __init__(self, name, sourcedir='', sources=[], **kw):
        # don't invoke the original build_ext for this special extension
        super().__init__(name, sources=sources, **kw)
        self.sourcedir = os.path.abspath(sourcedir)


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
        cfg = 'Debug' if self.debug else 'Release'

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        cmake_args = ['-DBUILD_SHARED_LIBS=OFF',
                       '-DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=FALSE',
                       '-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=FALSE'
                    ]
        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, cwd=self.build_temp, env=env)
        subprocess.check_call(['cmake', '--build', '.'], cwd=self.build_temp)


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
