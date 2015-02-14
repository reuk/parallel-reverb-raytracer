cmake_minimum_required(VERSION 3.0)

project(rayverb)

<<<<<<< HEAD
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Wall -std=c++11")
#set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Weverything -std=c++11")
=======
#set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Wall -std=c++11")
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Weverything -std=c++11")
>>>>>>> 8cb54b455040bcf3a3d693421d69809f96e3b221

include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

set(name parallel_raytrace)
set(sources main.cpp helpers.cpp rayverb.cpp filters.cpp kernel.cpp hrtf.cpp)

find_library(opencl_library OpenCL)
mark_as_advanced(opencl_library)
set(frameworks ${opencl_library})

set(libraries assimp fftw3f sndfile ${frameworks})

add_executable(${name} ${sources})

target_link_libraries(${name} ${libraries})
