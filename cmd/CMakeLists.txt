cmake_minimum_required(VERSION 3.0)

project(parallel_raytrace)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Wall -std=c++1y")

include_directories(
    ${CMAKE_SOURCE_DIR}/rayverb
    ${CMAKE_SOURCE_DIR}/include
)

set(name parallel_raytrace)
set(sources main.cpp)

add_executable(${name} ${sources})

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
find_library(sndfile_library sndfile)

target_link_libraries(${name} rayverb ${sndfile_library})

add_custom_target(manpage make COMMAND make rebuild WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS ${name} RUNTIME DESTINATION bin)
install(FILES parallel_raytrace.1 parallel_raytrace.1.md.pdf DESTINATION man)
