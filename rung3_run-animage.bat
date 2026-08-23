@echo off
REM Runs Animage carrying marks by rung three: one translation per region.
REM
REM This is the default, so it is the same program run-animage.bat gives you.
REM It exists so that the pair of them read as a pair.
REM
REM TEMPORARY. It goes when the rung is settled -- see CtgSettings::carry.

setlocal
set "ANIMAGE_CARRY=region"
call "%~dp0run-animage.bat" %*
