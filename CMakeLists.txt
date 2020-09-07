cmake_minimum_required(VERSION 3.5)

project(assimp_py)
find_package(PythonLibs REQUIRED)

set(ASSIMP_NO_EXPORT ON)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(assimp)


add_library(assimp_py SHARED assimp_py/assimp_py.c)


if(NOT DEFINED PYTHON_MOD_SUFFIX)
    execute_process(
        COMMAND python3-config --extension-suffix
        OUTPUT_VARIABLE _py_soabi
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS, "Module Suffix --> ${_py_soabi}")
    set(PYTHON_MOD_SUFFIX "${_py_soabi}" CACHE PATH INTERNAL)
    unset(_py_soabi)
endif()


set_target_properties(assimp_py PROPERTIES PREFIX ""
                                           SUFFIX "${PYTHON_MOD_SUFFIX}")

target_include_directories(assimp_py PRIVATE ${PYTHON_INCLUDE_DIRS})
target_link_libraries(assimp_py assimp ${PYTHON_LIBRARIES})