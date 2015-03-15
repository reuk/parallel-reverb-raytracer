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

install(TARGETS ${name} RUNTIME DESTINATION bin)

add_custom_target(manpage make WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

install(FILES parallel_raytrace.1 DESTINATION /usr/share/man/man1)
