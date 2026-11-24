@echo off
setlocal
where pwsh.exe >nul 2>&1
if errorlevel 1 (
  echo Install PowerShell 7 and add pwsh.exe to PATH. 1>&2
  exit /b 1
)
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\dev.ps1" %*
exit /b %errorlevel%
