@echo off
setlocal
echo Draw3 diagnostics package 3. Keep this window open until the resource comparisons finish.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-Draw3-Performance.ps1" %*
set "diagnostic_exit=%ERRORLEVEL%"
echo.
echo Collector exit code: %diagnostic_exit%
pause
exit /b %diagnostic_exit%
