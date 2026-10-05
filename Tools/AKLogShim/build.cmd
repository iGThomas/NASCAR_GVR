@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /LD /MT /O2 /W3 /D_CRT_SECURE_NO_WARNINGS AKLogShim.cpp /link /DEF:AKLogShim.def /OUT:AKLogShim.dll ole32.lib user32.lib
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo BUILD_OK
