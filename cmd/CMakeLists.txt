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

target_link_libraries(${name} rayverb)

install(TARGETS ${name} RUNTIME DESTINATION bin)
install(FILES parallel_raytrace.1 DESTINATION /usr/share/man/man1)
