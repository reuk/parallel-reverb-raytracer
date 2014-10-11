cmake_minimum_required(VERSION 3.0)

project(rayverb_master)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)

set(test_prefix basic)
set(test_obj ${test_prefix}.obj)
set(test_mtl ${test_prefix}.mtl)

set(test_file_flag -DTEST_FILE="${test_obj}")

#set(assimp_dir assimp-3.1.1)
#set(assimp_include_dir ${CMAKE_CURRENT_SOURCE_DIR}/${assimp_dir}/include)
#add_subdirectory(${assimp_dir})
set(assimp_include_dir)

add_subdirectory(rayverb)
add_subdirectory(gtest-1.7.0)
add_subdirectory(tests)
add_subdirectory(cmd)

file(COPY
    ${CMAKE_CURRENT_SOURCE_DIR}/${test_obj}
    ${CMAKE_CURRENT_SOURCE_DIR}/${test_mtl}
    DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
