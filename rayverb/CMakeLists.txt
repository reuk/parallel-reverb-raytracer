cmake_minimum_required(VERSION 3.0)

project(rayverb)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Wall -std=c++1y")

include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

add_library(rayverb helpers.cpp rayverb.cpp filters.cpp kernel.cpp hrtf.cpp)
