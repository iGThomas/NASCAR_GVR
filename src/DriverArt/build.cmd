@echo off
rem Builds GvrDriverArt.exe with the .NET Framework 4 compiler that ships with Windows (no SDK needed).
setlocal
set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
if not exist "%CSC%" (echo .NET Framework 4 compiler not found: %CSC% & exit /b 1)
cd /d "%~dp0"
"%CSC%" /nologo /optimize+ /platform:anycpu /target:exe /out:GvrDriverArt.exe /r:System.Drawing.dll /r:System.Core.dll GvrDriverArt.cs
exit /b %ERRORLEVEL%
