@echo off
REM Builds and runs both dock probes, one after the other, for a hand test.
REM
REM The same drag in each is what answers "is this fault ours or Qt's?".
REM window_probe is the real Animage window; dock_probe is plain Qt with none of
REM Animage in it. Neither can be driven from code -- setFloating does not enter
REM Qt's drag machinery -- so this needs a hand, which is why it is a .bat and
REM not a test.
REM
REM Each writes a log beside this file: window-probe.log and dock-probe.log.

setlocal
set "MSYS_BIN=C:\msys64\ucrt64\bin"
set "PATH=%MSYS_BIN%;%PATH%"
cd /d "%~dp0"

if not exist "build\CMakeCache.txt" (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || goto :failed
)

echo Building both probes...
cmake --build build --target window_probe || goto :failed
cmake --build build --target dock_probe || goto :failed

echo.
echo ================================================================
echo  1 of 2: the REAL Animage window.
echo.
echo    #57  drag Timeline right out of the window, let go, then drag
echo         it back in. Did it come back shorter than it left?
echo.
echo         Do NOT drag it taller first. That is the case that
echo         works, so an untouched dock is the one to tear off.
echo.
echo    #55  drag Layers from the right edge over to the left edge.
echo         Did it arrive a different width?
echo.
echo  Close the window when you are done.
echo ================================================================
echo.
"build\tests\window_probe.exe"

echo.
echo ================================================================
echo  2 of 2: PLAIN QT, with none of Animage in it.
echo.
echo  The same two drags: tear "Under" out and put it back, and move
echo  "Side" from the right edge to the left. Both start at Animage's
echo  sizes. If plain Qt does the same thing, the fault is Qt's; if it
echo  does not, it is ours.
echo.
echo  Close the window when you are done.
echo ================================================================
echo.
"build\tests\dock_probe.exe"

echo.
echo Logs written beside this file:
echo   window_probe.log
echo   dock_probe.log
pause
goto :eof

:failed
echo.
echo Build failed. See the messages above.
pause
exit /b 1
