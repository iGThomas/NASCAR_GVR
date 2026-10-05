@echo off
REM Build GvrDongleEmu -> GVRSCR28.dll (32-bit) for the NASCAR shell.
REM Must be 32-bit: the host (AMPlayer.exe) is a 2008 x86 process.
REM Static CRT (/MT) so this DLL needs no redistributable - it is loaded into a
REM process that links the ancient MSVC 7.1 runtimes.
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSPATH=%%i
if not defined VSPATH ( echo VISUAL STUDIO NOT FOUND & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 ( echo vcvars32 failed & exit /b 1 )
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /LD /MT /EHsc /O2 /W3 /D_CRT_SECURE_NO_WARNINGS ^
   /Fo:build\ /Fd:build\ GvrDongleEmu.cpp ^
   /link /DEF:GvrDongleEmu.def /OUT:build\GVRSCR28.dll /IMPLIB:build\GvrDongleEmu.lib ^
   user32.lib
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo BUILD_OK
endlocal
