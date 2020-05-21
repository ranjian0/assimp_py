import os
import conf
from sys import platform as _platform
from distutils.core import setup, Extension


# Enable ccache to speed up builds
ON_LINUX = "linux" in _platform
if ON_LINUX:
    os.environ['CC'] = 'ccache gcc'

# Windows
else:
    # Using clcache.exe, see: https://github.com/frerich/clcache

    # Insert path to clcache.exe into the path.

    prefix = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(prefix, "bin")

    print "Adding %s to the system path." % path
    os.environ['PATH'] = '%s;%s' % (path, os.environ['PATH'])

    clcache_exe = os.path.join(path, "clcache.exe")


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
                newcc_args = cc_args + ["-mmacosx-version-min=10.7", "-stdlib=libc++"]
        self._compile(obj, src, ext, newcc_args, extra_postargs, pp_opts)

    # convert to list, imap is evaluated on-demand
    pool = multiprocessing.pool.ThreadPool(N)
    list(pool.imap(_single_compile, objects))
    return objects


import distutils.ccompiler
distutils.ccompiler.CCompiler.compile = parallelCCompile


def main():
    setup(name="assimp",
          version="1.0.0",
          description="Python interface for the assimp library",
          author="<Ian Ichung'wah>",
          author_email="karanjaichungwa@gmail.com",
          ext_modules=[Extension("assimp",
                       sources=conf.ASSIMP_SOURCES + ["assimp.c"],
                       define_macros=conf.ASSIMP_DEFINES,
                       include_dirs=conf.ASSIMP_INCLUDE_DIRS)])

if __name__ == "__main__":
    main()
