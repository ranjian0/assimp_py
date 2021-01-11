cmake_minimum_required(VERSION 3.0)

project(assimp_py)
# find_package(PythonInterp REQUIRED)
find_package(PythonLibs REQUIRED)

set(CMAKE_C_STANDARD 99)
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
include_directories(assimp/include ${PYTHON_INCLUDE_DIRS})
add_library(assimp_py SHARED assimp_py.c)


# -- set the suffix of the build extension module
if(NOT DEFINED PYTHON_MOD_SUFFIX)
    execute_process(
        COMMAND python3-config --extension-suffix
        OUTPUT_VARIABLE _py_soabi
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(PYTHON_MOD_SUFFIX "${_py_soabi}" CACHE PATH INTERNAL)
    unset(_py_soabi)
endif()


set_target_properties(assimp_py PROPERTIES PREFIX ""
                                           LINKER_LANGUAGE C
                                           SUFFIX "${PYTHON_MOD_SUFFIX}")

# -- link assimp
target_link_libraries(assimp_py assimp)
# if(WIN32 OR APPLE)
#     target_link_libraries(assimp_py ${PYTHON_LIBRARIES})
# endif()