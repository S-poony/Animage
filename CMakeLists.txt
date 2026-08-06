cmake_minimum_required(VERSION 3.21)
project(Animage VERSION 0.1.0 LANGUAGES CXX)

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

# The core library and its tests never need Qt. Only the application does, so a
# missing Qt is a skipped target rather than a failed configure.
find_package(Qt6 6.5 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
  add_subdirectory(src/app)
else()
  message(STATUS "Qt 6 not found: building the core library and tests only")
endif()

if(ANIMAGE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
