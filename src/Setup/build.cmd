@echo off
REM Build Setup.exe - the stub of the one-file download. Tools\Make-Release.ps1 appends the release
REM zip to it (NASCAR-GVR-<version>-Setup.exe). 32-bit, static runtime, no other files needed.
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSPATH=%%i
if not defined VSPATH ( echo VISUAL STUDIO NOT FOUND & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\Setup.res Setup.rc
if errorlevel 1 ( echo RESOURCE COMPILE FAILED & exit /b 1 )
cl /nologo /MT /EHsc /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Fo:build\ /Fd:build\ Setup.cpp ^
   /link /OUT:build\Setup.exe /SUBSYSTEM:CONSOLE /MANIFEST:NO build\Setup.res
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo BUILD_OK
