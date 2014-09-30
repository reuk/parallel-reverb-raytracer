cmake_minimum_required(VERSION 2.8)

project(prt)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "bin/")
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11)

set(sources main.cpp)

add_executable(prt main.cpp)

find_library(opencl_library OpenCL)
mark_as_advanced(opencl_library)
set(frameworks ${opencl_library})

set(libraries assimp sndfile ${frameworks})

target_link_libraries(prt ${libraries})

file(COPY
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl
    DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
