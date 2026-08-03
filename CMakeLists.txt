cmake_minimum_required(VERSION 3.21)
project(Animage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ANIMAGE_BUILD_TESTS "Build the unit tests" ON)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)

if(ANIMAGE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
