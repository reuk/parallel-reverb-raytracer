cmake_minimum_required(VERSION 3.0)

project(tests)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/rayverb
    ${CMAKE_SOURCE_DIR}/rapidjson-master/include
    ${CMAKE_SOURCE_DIR}/gtest-1.7.0/include
    ${assimp_include_dir}
)

set(name tests)
set(sources main.cpp)

add_definitions(${test_file_flag})

add_executable(${name} ${sources})

find_library(opencl_library OpenCL)
mark_as_advanced(opencl_library)

set(frameworks ${opencl_library})

set(libraries assimp sndfile gtest ${frameworks})

target_link_libraries(${name} ${libraries})
