add_library(animage_core
  brush.cpp
  cel.cpp
  celfile.cpp
  color.cpp
  command.cpp
  compositor.cpp
  ctg.cpp
  ctg_fill.cpp
  document.cpp
  gridflow.cpp
  json.cpp
  lazybrush.cpp
  serialise.cpp
  track.cpp
)

target_include_directories(animage_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(animage_core PUBLIC cxx_std_20)

add_library(animage::core ALIAS animage_core)
