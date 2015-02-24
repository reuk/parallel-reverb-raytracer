cmake_minimum_required(VERSION 3.0)

project(tests)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/rayverb
    ${CMAKE_SOURCE_DIR}/gtest-1.7.0/include
)

set(name tests)
set(sources main.cpp ../rayverb/helpers.cpp ../rayverb/rayverb.cpp ../rayverb/kernel.cpp ../rayverb/filters.cpp ../rayverb/hrtf.cpp)

add_definitions(${test_file_flag})

add_executable(${name} ${sources})

find_library(opencl_library OpenCL)
mark_as_advanced(opencl_library)
set(frameworks ${opencl_library})

set(libraries assimp fftw3f sndfile gtest ${frameworks})

target_link_libraries(${name} ${libraries})
