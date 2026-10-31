@echo off
setlocal
set "INKEYS_DRAW3_ULW_FULL_UPDATE=1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-Win7.ps1"
endlocal
pause
