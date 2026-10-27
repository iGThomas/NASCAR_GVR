@echo off
rem ==========================================================================
rem  Double-click this file to install or update a GlobalVR arcade NASCAR game:
rem  NASCAR Racing (v1.1 disc) or NASCAR Team Racing (2.0.2 disc).
rem
rem  It asks Windows for administrator rights, then runs Install-NASCAR.ps1 from
rem  this folder, which asks which game (a mounted disc is preselected) and runs
rem  that game's installer. Nothing else is needed: no PowerShell window to open,
rem  no folder to type, no execution policy.
rem  Advanced options can still be passed, e.g.  Install.bat -Game 2.0 -NoShortcut
rem ==========================================================================
setlocal
cd /d "%~dp0"

rem already running as administrator?
fltmc >nul 2>&1
if errorlevel 1 (
    echo Asking Windows for administrator rights...
    set "SELF=%~f0"
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath $env:SELF -Verb RunAs"
    exit /b
)

rem -STA: lets the installer show the Windows folder pickers (needed on Windows 7)
powershell -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0Install-NASCAR.ps1" %*
echo.
echo You can close this window.
pause >nul
