cmake_minimum_required(VERSION 4.0)

project(assimp_py)
set(CMAKE_C_STANDARD 99)
if(UNIX AND NOT APPLE)
    # https://pybind11.readthedocs.io/en/stable/compiling.html#findpython-mode
    # XXX Gotcha for building manylinux wheels.
    find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
    # Uncomment for local builds 😩
    # find_package(Python REQUIRED COMPONENTS Interpreter Development)
else()
    if(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
        # Cross-compiled with -A ARM64 (cibuildwheel win_arm64 builds on amd64
        # runners): FindPython rejects the x64 host interpreter for the ARM64
        # target and no ARM64 python exists on the host. setup.py passes the
        # arch-independent include dir via -DPython_INCLUDE_DIRS instead
        # (the extension never links the python library on Windows).
        if(NOT Python_INCLUDE_DIRS)
            message(FATAL_ERROR "Python_INCLUDE_DIRS must be set for ARM64 cross builds")
        endif()
    else()
        # XXX Gotcha for building multiple python versions on windows.
        find_package(Python ${REQUESTED_PYTHON_VERSION} EXACT REQUIRED COMPONENTS Interpreter Development)
    endif()
endif()

# build for universal2
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")

# use ccache to speed up builds if it is available
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif(CCACHE_FOUND)

# needed for python lib
if(CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -funsigned-char")
endif()

if(MSVC)
    # Statically link the CRT so wheels are hermetic: no dependency on
    # msvcp140.dll/vcruntime140.dll (whose ARM64 variants do not exist on
    # cross-build hosts) and nothing for delvewheel to vendor. Safe here:
    # assimp is statically linked into the extension and no CRT objects
    # cross the python boundary.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /J")
endif()

# Workaround: assimp's bundled zlib (1.2.13) fails to compile against Xcode >= 16.3
# SDKs, where contrib/zlib/zutil.h's `#define fdopen(fd,mode) NULL` mangles the
# SDK's fdopen declaration in stdio.h (https://github.com/assimp/assimp/issues/6118).
# Pre-defining fdopen makes zutil.h's `#ifndef fdopen` guard skip the broken macro.
if(APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Dfdopen=fdopen")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dfdopen=fdopen")
endif()

# -- fetch assimp
# For offline builds, point FETCHCONTENT_SOURCE_DIR_ASSIMP at a local assimp checkout
include(FetchContent)
FetchContent_Declare(assimp
    URL https://github.com/assimp/assimp/archive/refs/tags/v6.0.5.tar.gz
    URL_HASH SHA256=edf3749559c2b7d1f758ffb66fc5bec62186221e623b7f2e8969f17ee46ecb6f
    SYSTEM
)

set(BUILD_SHARED_LIBS OFF)
set(ASSIMP_BUILD_ZLIB ON)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF)
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_WARNINGS_AS_ERRORS OFF)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF)

# XXX Uncomment the following lines to get lighter OBJ only build for development
# set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
# set(ASSIMP_BUILD_OBJ_IMPORTER ON)

FetchContent_MakeAvailable(assimp)

# Batch assimp's ~460 translation units into a handful of unity files:
# fewer compiler processes means faster builds everywhere (roughly halves
# native build times as well)
set_property(DIRECTORY ${assimp_SOURCE_DIR} PROPERTY UNITY_BUILD ON)
set_property(DIRECTORY ${assimp_SOURCE_DIR} PROPERTY UNITY_BUILD_BATCH_SIZE 32)

# build for universal2
set_property(DIRECTORY ${assimp_SOURCE_DIR} PROPERTY CMAKE_OSX_ARCHITECTURES "x86_64;arm64")

# -- build the python extension
include_directories(${assimp_SOURCE_DIR}/include ${assimp_BINARY_DIR}/include ${Python_INCLUDE_DIRS})
link_directories(${Python_LIBRARY_DIRS})
add_library(assimp_py SHARED src/assimp_py.c)

if(UNIX AND NOT APPLE)
    # For some reason, by default, we link to static zlib and rt,
    # explicitly linking z and rt prevents the error related to that 
    target_link_libraries(assimp_py assimp z rt)
elseif(APPLE)
    target_link_libraries(assimp_py assimp "-undefined dynamic_lookup")
else()
    target_link_libraries(assimp_py assimp)
endif()


# -- set the name of the build extension module
# EXTENSION_NAME is defined in setup.py
set_target_properties(
    assimp_py
    PROPERTIES
        PREFIX ""
        SUFFIX ""
        OUTPUT_NAME "${EXTENSION_NAME}"
)
