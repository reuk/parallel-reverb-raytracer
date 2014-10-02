cmake_minimum_required(VERSION 2.8)

project(prt)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "bin/")
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11)

include_directories("rapidjson-master/include")

set(lib_sources rayverb_lib.cpp)
set(lib_name simple_raytracer)

add_library(${lib_name} ${lib_sources})

set(exe_sources main.cpp)
set(exe_name parallel_raytrace)

add_executable(${exe_name} ${exe_sources})

find_library(opencl_library OpenCL)
mark_as_advanced(opencl_library)
set(frameworks ${opencl_library})

set(libraries ${lib_name} assimp sndfile ${frameworks})

target_link_libraries(${exe_name} ${libraries})

file(COPY
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl
    DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
