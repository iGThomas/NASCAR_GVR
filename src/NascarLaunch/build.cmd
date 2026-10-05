@echo off
REM Build NascarLaunch.exe - reads nascar_settings.ini, applies it to the registry,
REM and starts the cabinet shell (or a direct race). No game executable is modified.
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSPATH=%%i
if not defined VSPATH ( echo VISUAL STUDIO NOT FOUND & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\NascarLaunch.res NascarLaunch.rc
if errorlevel 1 ( echo RESOURCE COMPILE FAILED & exit /b 1 )
cl /nologo /MT /EHsc /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Fo:build\ /Fd:build\ NascarLaunch.cpp ^
   /link /OUT:build\NascarLaunch.exe /SUBSYSTEM:WINDOWS build\NascarLaunch.res ^
   user32.lib kernel32.lib gdi32.lib shell32.lib shlwapi.lib advapi32.lib
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo BUILD_OK
