cmake_minimum_required(VERSION 3.0)

set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)
set(CMAKE_BUILD_TYPE RelWithDebInfo)

project(rayverb_master)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)

add_subdirectory(rayverb)
add_subdirectory(cmd)
add_subdirectory(gtest-1.7.0)
add_subdirectory(tests)
