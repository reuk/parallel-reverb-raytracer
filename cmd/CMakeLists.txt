cmake_minimum_required(VERSION 3.0)

project(parallel_raytrace)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Wall -std=c++1y")

include_directories(
    ${CMAKE_SOURCE_DIR}/rayverb
    ${CMAKE_SOURCE_DIR}/include
)

set(name parallel_raytrace)
set(sources main.cpp)

find_library(opencl_library OpenCL)
mark_as_advanced(opencl_library)
set(frameworks ${opencl_library})

set(libraries rayverb assimp fftw3f sndfile ${frameworks})

add_executable(${name} ${sources})

target_link_libraries(${name} ${libraries})

install(TARGETS ${name}
    RUNTIME DESTINATION bin
)
