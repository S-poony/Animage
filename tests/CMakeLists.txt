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

# A measurement rather than a test: what an estimator does to a family of
# shapes, all moved the same known amount. Every other fixture moves one shape,
# which is how four rungs were asked the same question about a rectangle for a
# year without anyone noticing rung four cannot follow one. See issue #69.
add_executable(bench_shapes bench_shapes.cpp)
target_link_libraries(bench_shapes PRIVATE animage::core)

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

  # A report rather than a test: what the colour layer did on every drawing of a
  # real project, through the canvas so that the ladder and the budgets are the
  # ones a person gets. For "the scribble does not reach that drawing".
  add_executable(carry_probe carry_probe.cpp)
  target_link_libraries(carry_probe PRIVATE animage_ui)
  target_include_directories(carry_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  # The shortcut table, which needs Qt for QKeySequence and an application for
  # the platform's standard keys -- but no window at all.
  add_executable(test_shortcuts test_shortcuts.cpp)
  target_link_libraries(test_shortcuts PRIVATE animage_ui)
  target_include_directories(test_shortcuts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME test_shortcuts COMMAND test_shortcuts -platform offscreen)

  # Issue #53: what Qt does with a pen event nobody accepts. It injects at the
  # platform boundary through QWindowSystemInterface, which is the only place
  # promotion can be watched from -- and that header is a *private* one.
  #
  # **Asked for separately, and only here.** Putting GuiPrivate in the top-level
  # find_package made the whole of Qt optional-and-then-missing on any install
  # that ships no private headers, which is what CI has: Qt6_FOUND went false,
  # every GUI target was skipped, and packaging then failed with no application
  # to package. A test that cannot be built is a test that does not run; an
  # application that cannot be built is a broken release.
  # As its own package rather than another COMPONENTS list, so a failure here
  # cannot clobber Qt6_FOUND for anything configured after it.
  find_package(Qt6GuiPrivate QUIET)
  if(Qt6GuiPrivate_FOUND)
    add_executable(test_pen_promotion test_pen_promotion.cpp)
    target_link_libraries(test_pen_promotion PRIVATE animage_ui Qt6::GuiPrivate)
    target_include_directories(test_pen_promotion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_pen_promotion COMMAND test_pen_promotion -platform offscreen)
  else()
    message(STATUS "Qt private headers not found: skipping test_pen_promotion")
  endif()

  # A plain Qt window with docks in it and none of Animage, for deciding whether
  # a dock or layout fault is ours or Qt's. Built but never run by ctest: it
  # asserts nothing, needs a real display, and in places needs a real hand. See
  # the file's own header, and the issue #54 trap in docs/handover.md.
  #
  # It links Qt directly rather than animage_ui, because being free of this
  # program is the entire point of it.
  #
  # WidgetsPrivate is what lets it read QMainWindowLayout's drag bookkeeping,
  # and it is optional: without it the probe still builds and still shows the
  # geometry, which is the symptom. Asked for as its own package and never in
  # the top-level find_package -- see test_pen_promotion above for what that
  # mistake costs.
  add_executable(dock_probe WIN32 dock_probe.cpp)
  target_link_libraries(dock_probe PRIVATE Qt6::Widgets)
  find_package(Qt6WidgetsPrivate QUIET)
  if(Qt6WidgetsPrivate_FOUND)
    target_link_libraries(dock_probe PRIVATE Qt6::WidgetsPrivate)
    target_compile_definitions(dock_probe PRIVATE ANIMAGE_HAVE_QT_PRIVATE)
  else()
    message(STATUS "Qt private headers not found: dock_probe cannot read the drag state")
  endif()

  # The other half of dock_probe: the same readings taken from the real window,
  # so a hand doing the same drag in both says whether a dock fault is ours or
  # Qt's. Built but never run by ctest, for dock_probe's reasons.
  #
  # A separate target and not a flag on dock_probe, deliberately: that one must
  # stay free of Animage and this one is nothing but Animage, and one binary
  # cannot be both. See the header of window_probe.cpp.
  add_executable(window_probe WIN32 window_probe.cpp)
  target_link_libraries(window_probe PRIVATE animage_ui)
  target_include_directories(window_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  # The third probe, and the one for questions whose answer belongs to a tablet
  # driver rather than to Qt or to us: every pen and mouse event the canvas is
  # offered, and what the canvas made of each. Built but never run by ctest, for
  # dock_probe's reasons -- a hand is the only way in.
  add_executable(pen_probe WIN32 pen_probe.cpp)
  target_link_libraries(pen_probe PRIVATE animage_ui)
  target_include_directories(pen_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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

  # Whether the canvas gets slower the longer a session runs, and what makes it
  # come back. bench_zoom times a pan on a document that was just built; this
  # one times the same pan on a document that has been drawn on for a while,
  # which is the only difference the report describes.
  add_executable(bench_session bench_session.cpp)
  target_link_libraries(bench_session PRIVATE animage_ui)
  target_include_directories(bench_session PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(WIN32)
    # For GetProcessMemoryInfo: what the process is holding is half the
    # question, and historyBytes() only answers for the tiles.
    target_link_libraries(bench_session PRIVATE psapi)
  endif()

  # Whether a carried mark agrees with a colourist. Needs Qt only to open a
  # real project -- everything it measures is in core -- and that is the whole
  # point of it: bench_carry moves a shape this repository drew, and two
  # drawings by a person are never the same shape twice.
  add_executable(bench_hand bench_hand.cpp)
  target_link_libraries(bench_hand PRIVATE animage_ui)
  target_include_directories(bench_hand PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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
