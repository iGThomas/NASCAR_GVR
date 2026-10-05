<#
  Make-Release.ps1 - builds the download for a GitHub Release (developer tool).

  Packs ONLY what Install-NASCAR-GVR-Portable.ps1 reads at install time, in the same
  folder layout, into dist\NASCAR-GVR-<Version>.zip. Upload that zip as the release asset.

    .\Tools\Make-Release.ps1 -Version 1.0
#>
param([Parameter(Mandatory = $true)][string]$Version)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$name = "NASCAR-GVR-$Version"
$stage = Join-Path $repo "dist\$name"
$zip = Join-Path $repo "dist\$name.zip"

# everything the installer reads (keep in sync with Join-Path $SourceRoot ... in the installer)
$files = @(
    "Install-NASCAR-GVR-Portable.ps1",
    "Patches\nascar-bytepatches.txt",
    "Deploy\PLUSDE.dll", "Deploy\GvrSqlite.dll", "Deploy\sqlite3.dll", "Deploy\game.db",
    "Tools\unshield.exe", "Tools\Fix-GvrDirNames.ps1",
    "src\GvrIOShim\build\GvrIO.dll",
    "src\NascarLaunch\build\NascarLaunch.exe", "src\NascarLaunch\nascar_settings.ini", "src\NascarLaunch\NASCAR_GVR.ico",
    "src\GvrSqlite\GvrSqlite.cs",
    "Dependencies\DotNet11\dotnetfx.exe", "Dependencies\DotNet11\NDP1.1sp1-KB867460-X86.exe",
    "Dependencies\DXVK\d3d9.dll"
)

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
foreach ($f in $files) {
    $from = Join-Path $repo $f
    if (!(Test-Path $from)) { throw "missing: $f" }
    $to = Join-Path $stage $f
    New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
    Copy-Item -LiteralPath $from -Destination $to
}

# a short read-me inside the zip, so the download explains itself
@"
NASCAR Team Racing (GlobalVR arcade) - portable installer $Version

1. Insert or mount your NASCAR Team Racing GlobalVR game disc.
2. Open PowerShell as Administrator in this folder and run:
     Set-ExecutionPolicy -Scope Process Bypass
     .\Install-NASCAR-GVR-Portable.ps1
3. Answer the two questions (install folder, disc drive), then start
   "NASCAR Team Racing" from the desktop shortcut.

Settings: nascar_settings.ini in the install folder.
Full guide, controls and findings: see the project page on GitHub.
"@ | Set-Content -Path (Join-Path $stage "README.txt") -Encoding ASCII

Compress-Archive -Path $stage -DestinationPath $zip
Remove-Item -Recurse -Force $stage
"{0}  ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB)
