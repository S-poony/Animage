@echo off
REM Builds if needed, then runs the M0 pen latency harness.
REM Double-click this file, or run it from a terminal.

setlocal
set "MSYS_BIN=C:\msys64\ucrt64\bin"
set "PATH=%MSYS_BIN%;%PATH%"
cd /d "%~dp0"

if not exist "%MSYS_BIN%\cmake.exe" (
  echo.
  echo Could not find CMake at %MSYS_BIN%.
  echo Install the toolchain from an MSYS2 UCRT64 shell:
  echo   pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base
  echo.
  pause
  exit /b 1
)

if not exist "build\CMakeCache.txt" (
  echo Configuring...
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || goto :failed
)

echo Building...
cmake --build build --target animage_m0_latency || goto :failed

echo.
echo Starting. Draw with the pen. C clears, H hides the HUD, Q quits.
echo.
"build\src\app\animage_m0_latency.exe"
goto :eof

:failed
echo.
echo Build failed. See the messages above.
pause
exit /b 1
