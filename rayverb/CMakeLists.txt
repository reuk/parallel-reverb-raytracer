cmake_minimum_required(VERSION 3.0)

project(rayverb)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/rapidjson-master/include
    ${rapidjson_include_dir}
)

set(name parallel_raytrace)
set(sources main.cpp helpers.cpp rayverb.cpp filters.cpp kernel.cpp)

find_library(opencl_library OpenCL)
mark_as_advanced(opencl_library)
set(frameworks ${opencl_library})

set(libraries assimp fftw3f sndfile ${frameworks})

add_executable(${name} ${sources})

target_link_libraries(${name} ${libraries})
