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
echo  Add two tracks first (Track menu, Add track, twice). With one
echo  track the timeline dock is already at its own size hint and has
echo  no spare height to lose.
echo.
echo  Then, watching the panel sizes:
echo    #57  drag Layers right out of the window and let go.
echo         Did Timeline get shorter?
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
echo  The same two drags, on "Side" and "Under" this time. If plain Qt
echo  does the same thing, the fault is Qt's; if it does not, it is
echo  ours.
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
