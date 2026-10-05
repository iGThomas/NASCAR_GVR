@echo off
REM Build the GvrIO.dll shim (see GvrIOShim.cpp for what it fixes and why).
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSPATH=%%i
if not defined VSPATH ( echo VISUAL STUDIO NOT FOUND & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
if not exist build mkdir build
REM shared private-registry library: src\GvrPrivReg in the release, GIT\src\GvrPrivReg in the dev tree
set PRIVREG=..\GvrPrivReg
if not exist %PRIVREG%\GvrPrivReg.cpp set PRIVREG=..\..\..\GIT\src\GvrPrivReg
cl /nologo /LD /MT /EHa /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Fo:build\ /Fd:build\ GvrIOShim.cpp GvrIOPad.cpp GvrIOReg.cpp %PRIVREG%\GvrPrivReg.cpp /I%PRIVREG% ^
   /link /OUT:build\GvrIO.dll /DEF:GvrIOShim.def kernel32.lib user32.lib hid.lib setupapi.lib advapi32.lib
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo BUILD_OK
