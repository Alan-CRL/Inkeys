@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-Win7-Diagnostics.ps1"
set "diagnosticExit=%ERRORLEVEL%"
echo.
echo Diagnostic collector exit code: %diagnosticExit%
pause
exit /b %diagnosticExit%
