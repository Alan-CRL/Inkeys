@echo off
setlocal
set "StressShell="
for /f "delims=" %%P in ('where pwsh.exe 2^>nul') do if not defined StressShell set "StressShell=%%P"
if not defined StressShell if exist "%ProgramFiles%\PowerShell\7\pwsh.exe" set "StressShell=%ProgramFiles%\PowerShell\7\pwsh.exe"
if not defined StressShell set "StressShell=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
rem Manual CPU/GPU stress tool; respect the selected host's execution policy.
echo StressLoad PowerShell: %StressShell%
"%StressShell%" -NoLogo -NoProfile -File "%~dp0Start-StressLoad.ps1" %*
set "StressExit=%ERRORLEVEL%"
if not "%StressExit%"=="0" (
  echo.
  echo StressLoad build or start failed. See the message above.
  pause
)
exit /b %StressExit%
