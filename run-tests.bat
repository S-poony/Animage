@echo off
REM Builds and runs the core unit tests.

setlocal
set "MSYS_BIN=C:\msys64\ucrt64\bin"
set "PATH=%MSYS_BIN%;%PATH%"
cd /d "%~dp0"

if not exist "build\CMakeCache.txt" (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || goto :failed
)

cmake --build build || goto :failed
ctest --test-dir build --output-on-failure
pause
goto :eof

:failed
echo.
echo Build failed. See the messages above.
pause
exit /b 1
