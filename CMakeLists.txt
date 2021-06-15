cmake_minimum_required(VERSION 3.0)

project(assimp_py)
set(CMAKE_C_STANDARD 99)
if(LINUX)
    # Get suitable version on linux (gotcha for manylinux wheel builds)
    find_package(Python REQUIRED COMPONENTS Interpreter Development)
else()
    # Get exact python version on windows/mac
    find_package(Python ${REQUESTED_PYTHON_VERSION} EXACT REQUIRED COMPONENTS Interpreter Development)
endif()

# use ccache to speed up builds if it is available
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif(CCACHE_FOUND)

# needed for python lib
if(CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -funsigned-char")
endif()

if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /J")
endif()

add_subdirectory(assimp)

# -- build the python extension
include_directories(assimp/include ${Python_INCLUDE_DIRS})
link_directories(${Python_LIBRARY_DIRS})
add_library(assimp_py SHARED assimp_py.c)

target_link_libraries(assimp_py assimp)
if(APPLE)
    target_link_libraries(assimp_py "-undefined dynamic_lookup")
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