cmake_minimum_required(VERSION 3.0)

project(cmd)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/rayverb
    ${CMAKE_SOURCE_DIR}/rapidjson-master/include 
    ${assimp_include_dir}
)

set(name parallel_raytrace)
set(sources main.cpp helpers.cpp)

add_definitions(${test_file_flag})

add_executable(${name} ${sources})

find_library(opencl_library OpenCL)
mark_as_advanced(opencl_library)
set(frameworks ${opencl_library})

set(libraries rayverb gflags assimp sndfile ${frameworks})

target_link_libraries(${name} ${libraries})

