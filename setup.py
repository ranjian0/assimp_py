import os
import conf
import pathlib
import setuptools
from sys import platform as _platform
from distutils.core import setup, Extension


# monkey-patch for parallel compilation
import multiprocessing
import multiprocessing.pool


def parallelCCompile(self, sources, output_dir=None, macros=None, include_dirs=None,
                     debug=0, extra_preargs=None, extra_postargs=None, depends=None):

    # those lines are copied from distutils.ccompiler.CCompiler directly
    macros, objects, extra_postargs, pp_opts, build = self._setup_compile(
      output_dir, macros, include_dirs, sources, depends, extra_postargs)
    cc_args = self._get_cc_args(pp_opts, debug, extra_preargs)
    # parallel code
    N = 2 * multiprocessing.cpu_count()  # number of parallel compilations
    try:
        # On Unix-like platforms attempt to obtain the total memory in the
        # machine and limit the number of parallel jobs to the number of Gbs
        # of RAM (to avoid killing smaller platforms like the Pi)
        mem = os.sysconf('SC_PHYS_PAGES') * os.sysconf('SC_PAGE_SIZE')  # bytes
    except (AttributeError, ValueError):
        # Couldn't query RAM; don't limit parallelism (it's probably a well
        # equipped Windows / Mac OS X box)
        pass
    else:
        mem = max(1, int(round(mem / 1024**3)))  # convert to Gb
        N = min(mem, N)

    def _single_compile(obj):
        try:
            src, ext = build[obj]
        except KeyError:
            return
        newcc_args = cc_args
        if _platform == "darwin":
            if src.endswith('.cpp'):
                newcc_args = cc_args + ["-mmacosx-version-min=10.7", "-stdlib=libc++", "-std=c++11"]
        self._compile(obj, src, ext, newcc_args, extra_postargs, pp_opts)

    # convert to list, imap is evaluated on-demand
    pool = multiprocessing.pool.ThreadPool(N)
    list(pool.imap(_single_compile, objects))
    return objects


import distutils.ccompiler
distutils.ccompiler.CCompiler.compile = parallelCCompile


# The directory containing this file
HERE = pathlib.Path(__file__).parent
README = (HERE/"README.md").read_text()


def main():
    setup(name="assimp_py",
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
          ],
          packages=["assimp_py"],
          ext_modules=[Extension("assimp_py.assimp_py",
                       sources=conf.ASSIMP_SOURCES + ["assimp_py/assimp_py.c"],
                       define_macros=conf.ASSIMP_DEFINES,
                       include_dirs=conf.ASSIMP_INCLUDE_DIRS)])


if __name__ == "__main__":
    main()
