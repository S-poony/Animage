add_library(animage_core
  audio_render.cpp
  audio_track.cpp
  brush.cpp
  cel.cpp
  color.cpp
  command.cpp
  compositor.cpp
  ctg.cpp
  ctg_fill.cpp
  ctg_job.cpp
  ctg_solver.cpp
  document.cpp
  gridflow.cpp
  lazybrush.cpp
  selection.cpp
  track.cpp
  transform.cpp
)

target_include_directories(animage_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(animage_core PUBLIC cxx_std_20)

add_library(animage::core ALIAS animage_core)
