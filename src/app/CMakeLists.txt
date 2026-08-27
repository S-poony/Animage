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
  animage/layer_list.cpp
  animage/layer_list.h
  animage/dock_title_strip.cpp
  animage/dock_title_strip.h
  animage/floating_dock_frame.cpp
  animage/floating_dock_frame.h
  animage/scene_settings_dialog.cpp
  animage/scene_settings_dialog.h
  animage/shortcuts.cpp
  animage/shortcuts.h
  animage/shortcuts_dialog.cpp
  animage/shortcuts_dialog.h
  animage/image_import.cpp
  animage/image_import.h
  animage/project_io.cpp
  animage/project_io.h
  animage/export_sequence.cpp
  animage/export_sequence.h
  animage/exr_writer.cpp
  animage/exr_writer.h
  animage/crash_report.cpp
  animage/crash_report.h
)
target_link_libraries(animage_ui PUBLIC Qt6::Widgets animage::core)
target_include_directories(animage_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/animage)

# OpenEXR, via tinyexr: one vendored BSD-3 header, compiled in exr_writer.cpp
# and nowhere else. SYSTEM so that -Werror does not police somebody else's
# code, which is the whole reason the writer is a file of its own.
target_include_directories(animage_ui SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)

# And miniz (MIT) for the deflate that ZIP compression needs, vendored rather
# than linked.
#
# The first version used find_package(ZLIB REQUIRED), on the stated grounds that
# zlib is a transitive dependency of Qt and so must be present. That is wrong.
# Qt bundles zlib internally and ships neither the headers nor a CMake target,
# so Windows CI failed at configure with "Could NOT find ZLIB" while Linux and
# macOS -- which carry zlib for their own reasons -- passed. Vendoring settles
# it on every platform without a per-platform install step, and makes the
# compressed bytes identical everywhere, which three different system zlibs
# would not.
target_sources(animage_ui PRIVATE ${PROJECT_SOURCE_DIR}/third_party/miniz.c)

# tinyexr only calls miniz's mz_-prefixed entry points, so its zlib-named
# aliases are switched off. They would otherwise define compress2, uncompress
# and friends into a process that may already have them from the zlib Qt links
# -- a collision that resolves silently, and differently per platform.
target_compile_definitions(animage_ui PRIVATE MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

# SYSTEM headers still warn when the *implementation* is compiled, because at
# that point it is our translation unit. Silence these two rather than lowering
# the bar for the tree: a warning in our code is still a build failure.
if(MSVC)
  set_source_files_properties(animage/exr_writer.cpp ${PROJECT_SOURCE_DIR}/third_party/miniz.c
                              PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(animage/exr_writer.cpp ${PROJECT_SOURCE_DIR}/third_party/miniz.c
                              PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Qt defines `signals`, `slots` and `emit` as macros, and Track has a member
# called `slots` -- the name the design documents use for the track in time.
# The core is Qt-free by design and should not have to dodge another library's
# macros, so the interface uses Q_SIGNALS / Q_SLOTS / Q_EMIT instead.
target_compile_definitions(animage_ui PUBLIC QT_NO_KEYWORDS)

add_executable(animage animage/main.cpp)
target_link_libraries(animage PRIVATE animage_ui)
set_target_properties(animage PROPERTIES
  # A console window on Windows, for the same reason as the latency harness:
  # a prototype that crashes should be able to say so.
  WIN32_EXECUTABLE OFF
  # macOS will not run a loose executable that opens windows, and macdeployqt
  # has nowhere to put Qt without a bundle to put it in.
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_BUNDLE_NAME "Animage"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.animage.Animage"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# ------------------------------------------------------------------ the icon
#
# The mark lives in packaging/animage.svg and is rasterised by
# packaging/make-icons.py, not by the build: rendering an SVG needs a renderer,
# and Qt's own reads SVG Tiny, which has no <mask> and so drops part of the
# drawing. The three places it has to arrive are unrelated to each other.
#
# One: the window. Qt asks the application for the icon of every window it
# opens, and gets it from the PNG ladder compiled in here -- see main.cpp. This
# is the only one of the three that a plain `cmake --build` run can show you.
qt_add_resources(animage "icons"
  PREFIX "/icons"
  BASE ${PROJECT_SOURCE_DIR}/packaging/icons
  FILES
    ${PROJECT_SOURCE_DIR}/packaging/icons/animage-16.png
    ${PROJECT_SOURCE_DIR}/packaging/icons/animage-24.png
    ${PROJECT_SOURCE_DIR}/packaging/icons/animage-32.png
    ${PROJECT_SOURCE_DIR}/packaging/icons/animage-48.png
    ${PROJECT_SOURCE_DIR}/packaging/icons/animage-64.png
    ${PROJECT_SOURCE_DIR}/packaging/icons/animage-128.png
    ${PROJECT_SOURCE_DIR}/packaging/icons/animage-256.png
    ${PROJECT_SOURCE_DIR}/packaging/icons/animage-512.png
)

# Two: the executable, for Explorer and the taskbar, which read the file rather
# than ask the process.
if(WIN32)
  target_sources(animage PRIVATE ${PROJECT_SOURCE_DIR}/packaging/animage.rc)
  # The .rc names animage.ico beside it. windres looks next to the script and
  # would find it anyway; Microsoft's rc.exe searches the working directory and
  # its include path instead, and the working directory under a Visual Studio
  # generator is the build tree. So the directory is named.
  target_include_directories(animage PRIVATE ${PROJECT_SOURCE_DIR}/packaging)
endif()

# Three: the bundle, for Finder and the Dock. The property names the file, and
# the source entry is what actually copies it into Contents/Resources.
if(APPLE)
  set_target_properties(animage PROPERTIES MACOSX_BUNDLE_ICON_FILE animage.icns)
  target_sources(animage PRIVATE ${PROJECT_SOURCE_DIR}/packaging/animage.icns)
  set_source_files_properties(${PROJECT_SOURCE_DIR}/packaging/animage.icns
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

# Linux has a fourth place, and it is not the build's: the desktop file names
# an icon that the desktop looks up by name in the icon theme, so it is the
# install that has to put the SVG there. See the AppImage step in the CI
# workflow, which installs packaging/animage.svg under hicolor.

# Enough of an install for the packaging step to have something to gather up.
include(GNUInstallDirs)
install(TARGETS animage
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
