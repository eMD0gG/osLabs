cmake_minimum_required(VERSION 3.15)
project(lab2 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(lib STATIC lib.c)
target_include_directories(lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test main.c)
target_link_libraries(test PRIVATE lib)

if(WIN32)
    target_compile_definitions(lib PRIVATE WIN32_LEAN_AND_MEAN)

else()
    target_compile_options(lib PRIVATE -Wall -Wextra)
    target_compile_options(test PRIVATE -Wall -Wextra)
endif()