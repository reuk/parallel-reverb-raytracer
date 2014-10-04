cmake_minimum_required(VERSION 3.0)

project(rayverb)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

set(name rayverb)
set(sources rayverb.cpp)

add_library(${name} ${sources})

file(COPY
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl
    DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
