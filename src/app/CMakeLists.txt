set(CMAKE_AUTOMOC ON)

# M0: the latency harness. Deliberately standalone -- it links nothing of ours,
# so it measures Qt and the driver rather than our code.
add_executable(animage_m0_latency
  latency/main.cpp
  latency/latency_canvas.cpp
  latency/latency_canvas.h
)
target_link_libraries(animage_m0_latency PRIVATE Qt6::Widgets)
target_include_directories(animage_m0_latency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/latency)

# A console window on Windows: the device listing and the report on exit are
# part of what the tool is for.
set_target_properties(animage_m0_latency PROPERTIES WIN32_EXECUTABLE OFF)

# M2/M3: the interface, as a library so that tests can drive it offscreen. A
# widget that can only be exercised by a human clicking on it is a widget whose
# crashes are found by the human.
add_library(animage_ui STATIC
  animage/main_window.cpp
  animage/main_window.h
  animage/canvas_widget.cpp
  animage/canvas_widget.h
  animage/timeline_widget.cpp
  animage/timeline_widget.h
  animage/scene_settings_dialog.cpp
  animage/scene_settings_dialog.h
  animage/crash_report.cpp
  animage/crash_report.h
)
target_link_libraries(animage_ui PUBLIC Qt6::Widgets animage::core)
target_include_directories(animage_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/animage)

# Qt defines `signals`, `slots` and `emit` as macros, and Track has a member
# called `slots` -- the name the design documents use for the track in time.
# The core is Qt-free by design and should not have to dodge another library's
# macros, so the interface uses Q_SIGNALS / Q_SLOTS / Q_EMIT instead.
target_compile_definitions(animage_ui PUBLIC QT_NO_KEYWORDS)

add_executable(animage animage/main.cpp)
target_link_libraries(animage PRIVATE animage_ui)
set_target_properties(animage PROPERTIES WIN32_EXECUTABLE OFF)
