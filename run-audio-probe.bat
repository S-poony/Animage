@echo off
REM Builds and runs the audio probe, for a hand test: does sound come out?
REM
REM Everything else about the audio device can be checked by machine and is --
REM `animage.exe --audio-check` says whether a backend loaded and what outputs
REM it found, and the probe's own table says what processedUSecs counts. None of
REM that can tell you a speaker made a noise. That needs an ear, which is why
REM this is a .bat and not a test.
REM
REM **And it is a .bat for a second reason.** audio_probe.exe links Qt, and Qt
REM lives in the MSYS2 tree -- so run from anywhere that does not have
REM C:\msys64\ucrt64\bin on PATH, Windows cannot find Qt6Core.dll and stops the
REM program before a line of it runs. The dialog says the code execution cannot
REM proceed because Qt6Core.dll was not found, which reads as "Qt is not
REM installed" and is nothing of the kind: it is installed and not on the path.
REM That is what the PATH line below is for, and it is the same line every other
REM script beside this one opens with.

setlocal
set "MSYS_BIN=C:\msys64\ucrt64\bin"
set "PATH=%MSYS_BIN%;%PATH%"
cd /d "%~dp0"

if not exist "build\CMakeCache.txt" (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || goto :failed
)

echo Building the probe...
cmake --build build --target audio_probe || goto :failed
if not exist "build\tests\audio_probe.exe" (
  echo.
  echo audio_probe was not built, which means Qt Multimedia was not found when
  echo this was configured. It is optional and asked for on its own, so the rest
  echo of the program builds without it. To install it:
  echo.
  echo   pacman -S --needed mingw-w64-ucrt-x86_64-qt6-multimedia mingw-w64-ucrt-x86_64-qt6-multimedia-ffmpeg
  echo.
  echo Then delete build\CMakeCache.txt and run this again, so that CMake looks
  echo for it a second time.
  pause
  exit /b 1
)

echo.
echo ================================================================
echo  Three seconds of a 440 Hz tone, and a table.
echo.
echo  THE QUESTION: do you hear it?
echo.
echo  Nothing else here can answer that. If you do, the audio path
echo  works on this machine end to end. If you do not, the table
echo  still says whether the samples were taken -- a `handed` column
echo  that climbs is a device accepting audio that nothing plays.
echo.
echo  Check the volume and which output Windows is pointed at first;
echo  the probe prints the device it opened on its first line, and
echo  `audio_probe.exe --list` shows the others.
echo ================================================================
echo.
"build\tests\audio_probe.exe" --seconds 3

echo.
pause
goto :eof

:failed
echo.
echo Build failed. See the messages above.
pause
exit /b 1
