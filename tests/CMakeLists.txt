function(animage_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE animage::core)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

animage_test(test_brush)
animage_test(test_ctg)
animage_test(test_ctg_solver)
animage_test(test_lazybrush)
animage_test(test_model)
animage_test(test_track)
animage_test(test_selection)
animage_test(test_transform)
animage_test(test_undo)
animage_test(test_color)

# A stopwatch rather than a test, so it is built but not run by ctest.
add_executable(bench_composite bench_composite.cpp)
target_link_libraries(bench_composite PRIVATE animage::core)

# A measurement rather than a test: how far a mark can be carried before it
# stops landing. See docs/scribbles-through-time.md, part 2.
add_executable(bench_carry bench_carry.cpp)
target_link_libraries(bench_carry PRIVATE animage::core)

# What a transform costs, and where. Written before anything was optimised.
add_executable(bench_transform bench_transform.cpp)
target_link_libraries(bench_transform PRIVATE animage::core)

# Interface tests need Qt, and run offscreen so they work on a build machine
# with no display.
# Saving and loading is the application's half -- ProjectIO, Qt's JSON and all
# -- so its tests live with the other interface tests.
if(TARGET animage_ui)
  add_executable(test_canvas test_canvas.cpp)
  target_link_libraries(test_canvas PRIVATE animage_ui)
  target_include_directories(test_canvas PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  # Declarations only -- the implementation is compiled once, in animage_ui.
  # The EXR export test reads its own output back, which needs the loader.
  target_include_directories(test_canvas SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
  add_test(NAME test_canvas COMMAND test_canvas -platform offscreen)

  add_executable(test_render test_render.cpp)
  target_link_libraries(test_render PRIVATE animage_ui)
  target_include_directories(test_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_render COMMAND test_render -platform offscreen)

  # The shortcut table, which needs Qt for QKeySequence and an application for
  # the platform's standard keys -- but no window at all.
  add_executable(test_shortcuts test_shortcuts.cpp)
  target_link_libraries(test_shortcuts PRIVATE animage_ui)
  target_include_directories(test_shortcuts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_shortcuts COMMAND test_shortcuts -platform offscreen)

  # Issue #53: what Qt does with a pen event nobody accepts. GuiPrivate for the
  # same reason -- this injects at the platform boundary, which is the only place
  # promotion can be watched from.
  add_executable(test_pen_promotion test_pen_promotion.cpp)
  target_link_libraries(test_pen_promotion PRIVATE animage_ui Qt6::GuiPrivate)
  target_include_directories(test_pen_promotion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_pen_promotion COMMAND test_pen_promotion -platform offscreen)

  # Issue #50: a floating panel's title bar has to be Qt's own, or a pen cannot
  # press it. Plain widget state, so no private Qt headers are needed.
  add_executable(test_floating_dock_frame test_floating_dock_frame.cpp)
  target_link_libraries(test_floating_dock_frame PRIVATE animage_ui)
  target_include_directories(test_floating_dock_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_floating_dock_frame COMMAND test_floating_dock_frame -platform offscreen)

  add_executable(test_serialise test_serialise.cpp)
  target_link_libraries(test_serialise PRIVATE animage_ui)
  target_include_directories(test_serialise PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_serialise COMMAND test_serialise)

  add_executable(test_hostile test_hostile.cpp)
  target_link_libraries(test_hostile PRIVATE animage_ui)
  target_include_directories(test_hostile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_hostile COMMAND test_hostile)

  # Stopwatches, like bench_composite: built, never run by ctest.
  add_executable(bench_save bench_save.cpp)
  target_link_libraries(bench_save PRIVATE animage_ui)

  add_executable(bench_zoom bench_zoom.cpp)
  target_link_libraries(bench_zoom PRIVATE animage_ui)

  # What playback costs and what it drops. Drives the whole window rather than
  # the canvas alone, because a frame change is a slot change, a layer-flag
  # refresh, a full-cache repaint and a playhead -- and timing only the canvas
  # is the bench_composite mistake one function further out.
  add_executable(bench_playback bench_playback.cpp)
  target_link_libraries(bench_playback PRIVATE animage_ui)

  # Pictures of the interface rather than measurements of it, and the same
  # deal: built, never run by ctest, run by hand before and after anything
  # that touches the canvas. Nothing in it asserts and nothing depends on it,
  # so a situation added to chase one bug can be deleted again freely --
  # which is the point. See the comment at the top of shots.cpp.
  add_executable(shots shots.cpp)
  target_link_libraries(shots PRIVATE animage_ui)
endif()
