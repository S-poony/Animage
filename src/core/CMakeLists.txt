add_library(animage_core
  cel.cpp
  color.cpp
  command.cpp
  document.cpp
  timeline.cpp
)

target_include_directories(animage_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(animage_core PUBLIC cxx_std_20)

add_library(animage::core ALIAS animage_core)
