cmake_minimum_required(VERSION 3.0)

project(rayverb)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

set(name rayverb)
set(sources rayverb.cpp)

add_library(${name} ${sources})

find_library(opencl_library OpenCL)
mark_as_advanced(opencl_library)
set(frameworks ${opencl_library})

set(libraries assimp fftw3f ${frameworks})

target_link_libraries(${name} ${libraries})

file(COPY
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl
    DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
