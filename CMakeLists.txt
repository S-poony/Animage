cmake_minimum_required(VERSION 3.21)
# C is here only for miniz, the deflate implementation the EXR writer uses. See
# src/app/CMakeLists.txt: it is vendored rather than linked because Qt bundling
# zlib does not make zlib available to link against, which cost a red Windows
# build to establish.
project(Animage VERSION 0.1.0 LANGUAGES C CXX)

# The resource script that gives animage.exe its icon. Visual Studio generators
# enable RC of their own accord; MinGW ones do not, and the file would be
# handed to the C compiler instead. Asked for here rather than in
# src/app/CMakeLists.txt so that it is enabled before any target is defined.
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ANIMAGE_BUILD_TESTS "Build the unit tests" ON)

# A warning is a bug the compiler saw first, and a tree that builds despite
# them ships bugs it has been told about. Denied here rather than policed: the
# build does not finish. Off for a compiler that invents new warnings on code
# that used to be clean, not as the default.
option(ANIMAGE_WERROR "Treat compiler warnings as errors" ON)

# The tests are the memory-safety story, and they can only tell it while the
# sanitizers are listening. On by default so that a plain build and test run
# catches out-of-bounds access, use-after-free and undefined behaviour; the
# packaging build in CI turns it off so the binaries it ships do not carry a
# sanitizer runtime.
option(ANIMAGE_SANITIZE "Build with ASan and UBSan" ON)

# For CI, and for nothing else. The probes below fall back quietly when a
# sanitizer runtime will not link -- which is right on a development machine,
# where MSYS2's GCC has neither and refusing to build would help nobody. It is
# exactly wrong in the jobs whose whole purpose is to run the tests under a
# sanitizer: there the fallback is a green run that proves nothing, and a green
# run that proves nothing is worse than a red one. Turned on, the same
# conditions raise a fatal error instead.
option(ANIMAGE_REQUIRE_SANITIZERS
       "Fail configuration if the sanitizers asked for do not link" OFF)

# Says what a missing sanitizer runtime costs, as a warning or as a refusal.
function(animage_no_sanitizer why)
  if(ANIMAGE_REQUIRE_SANITIZERS)
    message(FATAL_ERROR "ANIMAGE_REQUIRE_SANITIZERS is ON and ${why}")
  else()
    message(WARNING "ANIMAGE_SANITIZE is ON but ${why}")
  endif()
endfunction()

if(MSVC)
  add_compile_options(/W4 /permissive-)
  # C4458: a parameter named `id` in a struct whose own field is `id`. Forty-odd
  # of them, all of the form `findLayer(LayerId id)`, and none is a bug: the
  # member is never named inside those functions, so there is nothing for the
  # parameter to hide. The alternative is renaming every one to say twice what
  # its type already says.
  add_compile_options(/wd4458)
  # C4996 on fopen and getenv: Microsoft deprecates the C standard library in
  # favour of _s variants that exist on no other platform. Taking the advice
  # would make the crash reporter and the test harness Windows-only to silence
  # a warning about code that is already correct everywhere.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  if(ANIMAGE_WERROR)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(ANIMAGE_WERROR)
    add_compile_options(-Werror)
  endif()
endif()

if(ANIMAGE_SANITIZE)
  include(CheckCXXSourceCompiles)
  if(MSVC)
    # MSVC has AddressSanitizer and no UndefinedBehaviorSanitizer; the runtime
    # is not always present, so probe for it.
    set(CMAKE_REQUIRED_FLAGS "/fsanitize=address")
    check_cxx_source_compiles("int main() { return 0; }" ANIMAGE_ASAN_OK)
    unset(CMAKE_REQUIRED_FLAGS)
    if(ANIMAGE_ASAN_OK)
      add_compile_options(/fsanitize=address)
      add_link_options(/fsanitize=address)
      message(STATUS "Sanitizers: AddressSanitizer")
    else()
      animage_no_sanitizer("this MSVC cannot link ASan")
    endif()
  else()
    # -fno-sanitize-recover is what turns a sanitizer report into a test
    # failure: without it UBSan prints and carries on, and the test passes.
    set(CMAKE_REQUIRED_FLAGS
        "-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer")
    check_cxx_source_compiles("int main() { return 0; }" ANIMAGE_ASAN_UBSAN_OK)
    unset(CMAKE_REQUIRED_FLAGS)
    if(ANIMAGE_ASAN_UBSAN_OK)
      add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined
                          -fno-omit-frame-pointer)
      add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined
                       -fno-omit-frame-pointer)
      message(STATUS "Sanitizers: AddressSanitizer + UndefinedBehaviorSanitizer")
    else()
      # Some distributions ship a compiler without the ASan runtime (Fedora's
      # gcc is one). UBSan alone still catches the undefined behaviour, so
      # fall back to it rather than silently building without any -- on a
      # development machine. In CI it is half the cover the job claims to give,
      # so the fallback is a refusal there instead.
      if(ANIMAGE_REQUIRE_SANITIZERS)
        message(FATAL_ERROR "ANIMAGE_REQUIRE_SANITIZERS is ON and ASan does not "
                            "link with this compiler; UBSan alone is half the cover")
      endif()
      set(CMAKE_REQUIRED_FLAGS
          "-fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer")
      check_cxx_source_compiles("int main() { return 0; }" ANIMAGE_UBSAN_OK)
      unset(CMAKE_REQUIRED_FLAGS)
      if(ANIMAGE_UBSAN_OK)
        add_compile_options(-fsanitize=undefined -fno-sanitize-recover=undefined
                            -fno-omit-frame-pointer)
        add_link_options(-fsanitize=undefined)
        message(STATUS "Sanitizers: UndefinedBehaviorSanitizer only (no ASan runtime here)")
      else()
        animage_no_sanitizer("no sanitizer runtime links; tests will not detect memory or UB errors")
      endif()
    endif()
  endif()
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
