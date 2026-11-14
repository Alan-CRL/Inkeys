@echo off
setlocal
set "DiagnosticShell="
for /f "delims=" %%P in ('where pwsh.exe 2^>nul') do if not defined DiagnosticShell set "DiagnosticShell=%%P"
if not defined DiagnosticShell if exist "%ProgramFiles%\PowerShell\7\pwsh.exe" set "DiagnosticShell=%ProgramFiles%\PowerShell\7\pwsh.exe"
if not defined DiagnosticShell if exist "%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe" set "DiagnosticShell=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe"
if not defined DiagnosticShell set "DiagnosticShell=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
rem Default round count belongs to PS1; respect the selected host's execution policy.
echo Diagnostic PowerShell: %DiagnosticShell%
"%DiagnosticShell%" -NoLogo -NoProfile -File "%~dp0Start-ShutdownDiagnostic.ps1" %*
set "DiagnosticExit=%ERRORLEVEL%"
echo.
if not "%DiagnosticExit%"=="0" echo Diagnostic preparation or capture failed. Keep the reported log folder.
pause
exit /b %DiagnosticExit%
