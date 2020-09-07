cmake_minimum_required(VERSION 3.5)

project(assimp_py)
find_package(PythonLibs REQUIRED)

set(ASSIMP_NO_EXPORT ON)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(assimp)


add_library(assimp_py SHARED assimp_py/assimp_py.c)

set_target_properties(
    assimp_py
    PROPERTIES
        PREFIX ""
        OUTPUT_NAME assimp_py
        # LINKER_LANGUAGE C
        # LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        # RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin  # only needed on windows
)

if(WIN32)
    # python modules use this
    set_target_properties(
        assimp_py
        PROPERTIES
        SUFFIX ".pyd"
    )
endif()

target_include_directories(assimp_py PRIVATE ${PYTHON_INCLUDE_DIRS})
target_link_libraries(assimp_py assimp ${PYTHON_LIBRARIES})