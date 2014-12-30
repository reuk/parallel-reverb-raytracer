cmake_minimum_required(VERSION 3.0)

project(rayverb_master)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)

set(rapidjson_dir rapidjson-master)
set(rapidjson_include_dir ${CMAKE_CURRENT_SOURCE_DIR}/${rapidjson_dir}/include)

add_subdirectory(rayverb)
add_subdirectory(gtest-1.7.0)
add_subdirectory(tests)
