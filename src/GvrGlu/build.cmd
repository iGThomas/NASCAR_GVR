@echo off
REM Build glu32.dll - loads GvrIO.dll into the menus without injection (Shell\bin\glu32.dll).
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSPATH=%%i
if not defined VSPATH ( echo VISUAL STUDIO NOT FOUND & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /LD /MT /O2 /W3 /GS- /D_CRT_SECURE_NO_WARNINGS /Fo:build\ /Fd:build\ GvrGlu.cpp ^
   /link /DEF:GvrGlu.def /OUT:build\glu32.dll /IMPLIB:build\glu32.lib kernel32.lib
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo BUILD_OK
