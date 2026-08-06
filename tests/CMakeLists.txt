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
animage_test(test_undo)
animage_test(test_color)

# A stopwatch rather than a test, so it is built but not run by ctest.
add_executable(bench_composite bench_composite.cpp)
target_link_libraries(bench_composite PRIVATE animage::core)

# A measurement rather than a test: how far a mark can be carried before it
# stops landing. See docs/scribbles-through-time.md, part 2.
add_executable(bench_carry bench_carry.cpp)
target_link_libraries(bench_carry PRIVATE animage::core)

# Interface tests need Qt, and run offscreen so they work on a build machine
# with no display.
# Saving and loading is the application's half -- ProjectIO, Qt's JSON and all
# -- so its tests live with the other interface tests.
if(TARGET animage_ui)
  add_executable(test_canvas test_canvas.cpp)
  target_link_libraries(test_canvas PRIVATE animage_ui)
  target_include_directories(test_canvas PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_canvas COMMAND test_canvas -platform offscreen)

  add_executable(test_render test_render.cpp)
  target_link_libraries(test_render PRIVATE animage_ui)
  target_include_directories(test_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_render COMMAND test_render -platform offscreen)

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
endif()
