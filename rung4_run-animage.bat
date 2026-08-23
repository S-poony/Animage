@echo off
REM Runs Animage carrying marks by rung four: the as-rigid-as-possible lattice.
REM
REM Rung four never picks one translation, so it is the only rung that can be
REM right about two things that moved differently. It costs about 195 ms an
REM estimate against rung three's 41, and that is off the interface thread.
REM
REM TEMPORARY. It goes when the rung is settled -- see CtgSettings::carry.

setlocal
set "ANIMAGE_CARRY=lattice"
call "%~dp0run-animage.bat" %*
