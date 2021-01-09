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
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        cmake_args = ['-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + extdir,
                      '-DPYTHON_EXECUTABLE=' + sys.executable]

        cfg = 'Debug' if self.debug else 'Release'
        build_args = ['--config', cfg]

        if platform.system() == "Windows":
            cmake_args += ['-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{}={}'.format(cfg.upper(), extdir)]
            if sys.maxsize > 2**32:
                cmake_args += ['-A', 'x64']
            build_args += ['--', '/m']
        else:
            cmake_args += ['-DCMAKE_BUILD_TYPE=' + cfg]
            build_args += ['--', '-j3']

        env = os.environ.copy()
        env['CXXFLAGS'] = '{} -DVERSION_INFO=\\"{}\\"'.format(env.get('CXXFLAGS', ''),
                                                              self.distribution.get_version())
        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        cmake_args += ['-DPYTHON_INCLUDE_DIR={}'.format(get_python_inc()),
                       '-DPYTHON_LIBRARY={}'.format(get_config_var('LIBDIR'))]

        # subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, cwd=self.build_temp, env=env)
        subprocess.check_call(['cmake', '--build', '.'] + build_args, cwd=self.build_temp)


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
        "Programming Language :: Python :: 3.6"
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8"
    ],
    ext_modules=[CMakeExtension("assimp_py")],
    cmdclass={
        'build_ext': CMakeBuild,
    }
)
