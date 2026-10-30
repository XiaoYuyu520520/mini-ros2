@echo off
rem racecar mapping: slam_toolbox on config/slam_toolbox.yaml - one line around tools\racecar_ctl.py, which is the same code the
rem Linux twin runs (gmapping.sh), so this desk and the robot behave alike.
rem See tools\racecar_ctl.py for the options; everything is passed through.
setlocal EnableExtensions
set "PY=python"
where python >nul 2>&1 || set "PY=py -3"
%PY% "%~dp0tools\racecar_ctl.py" slam %*
exit /b %ERRORLEVEL%
