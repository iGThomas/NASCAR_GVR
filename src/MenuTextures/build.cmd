@echo off
rem Builds GvrMenuTextures.exe with the .NET Framework 4 compiler that ships with Windows (no SDK needed).
setlocal
set FW=%WINDIR%\Microsoft.NET\Framework\v4.0.30319
set CSC=%FW%\csc.exe
if not exist "%CSC%" (echo .NET Framework 4 compiler not found: %CSC% & exit /b 1)
cd /d "%~dp0"
"%CSC%" /nologo /optimize+ /platform:anycpu /target:winexe /out:GvrMenuTextures.exe ^
  /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Core.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll ^
  /r:"%FW%\WPF\PresentationCore.dll" /r:"%FW%\WPF\WindowsBase.dll" /r:System.Xaml.dll ^
  Program.cs EditorForm.cs Mods.cs AmTextures.cs
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo BUILD_OK
