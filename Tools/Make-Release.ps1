<#
  Make-Release.ps1 - builds the download for a GitHub Release (developer tool).

  One zip for both games: NASCAR Racing (v1.1 disc) and NASCAR Team Racing (2.0.2 disc).
  Install.bat runs Install-NASCAR.ps1, which asks which game and runs its installer. Packs ONLY
  what the installers read at install time, in the same folder layout, into
  dist\NASCAR-GVR-<Version>.zip. Upload that zip as the release asset.

    .\Tools\Make-Release.ps1 -Version 1.2.0
#>
param([Parameter(Mandatory = $true)][string]$Version)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$name = "NASCAR-GVR-$Version"
$stage = Join-Path $repo "dist\$name"
$zip = Join-Path $repo "dist\$name.zip"

# everything the installers read. KEEP IN SYNC with $ReleaseFiles in Install-NASCAR-GVR-Portable.ps1
# and Install-NASCAR-Team-Racing-Portable.ps1 (their release guard stops an install when any of
# their files is missing - e.g. removed by antivirus).
$files = @(
    "Install.bat", "Install-NASCAR.ps1",
    # NASCAR Racing (v1.1 disc)
    "Install-NASCAR-GVR-Portable.ps1", "Patches\nascar-bytepatches.txt",
    "Deploy\PLUSDE.dll", "Deploy\GvrSqlite.dll", "Deploy\sqlite3.dll", "Deploy\game.db",
    # NASCAR Team Racing (2.0.2 disc)
    "Install-NASCAR-Team-Racing-Portable.ps1", "Patches\nascar-202-bytepatches.txt",
    "Deploy-2.0.2\PLUSDE.dll", "Deploy-2.0.2\GvrSqlite.dll", "Deploy-2.0.2\sqlite3.dll", "Deploy-2.0.2\game.db",
    "Deploy-2.0.2\AKLogShim.dll",
    # both
    "Tools\unshield.exe", "Tools\Fix-GvrDirNames.ps1", "Tools\Check-DotNet11.ps1",
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
NASCAR GVR $Version - GlobalVR arcade NASCAR games on a normal Windows PC

Installs either game (or both, side by side):
  NASCAR Racing       - needs the NASCAR v1.1 game disc (part 050-0136-01)
  NASCAR Team Racing  - needs the NASCAR Team Racing 2.0.2 game disc

1. Mount the game disc ISO (right-click it, Mount), or insert the disc.
2. Double-click Install.bat in this folder and allow administrator rights.
3. Choose the game (a mounted disc is already selected), then pick where to
   install. When it says it is done, start the game from its desktop shortcut.

Updating: double-click Install.bat, choose the game and pick its existing install
folder - no disc needed; scores, settings and pedal calibration are kept.

Settings: nascar_settings.ini in the install folder.
Problems: set [Debug] Log=true in nascar_settings.ini, reproduce the problem once,
then zip the LOG folder from the install folder and share it.
Full guide, controls and findings: see the project page on GitHub.
"@ | Set-Content -Path (Join-Path $stage "README.txt") -Encoding ASCII

# Compress-Archive (PowerShell 5.1) writes "\" into entry names: 7-Zip and Linux unzip then make
# files named "NASCAR...\Tools\unshield.exe" instead of folders. Write "/" entries ourselves.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$base = Split-Path -Parent $stage
$archive = [System.IO.Compression.ZipFile]::Open($zip, "Create")
try {
    foreach ($item in Get-ChildItem -LiteralPath $stage -Recurse -File) {
        $entry = $item.FullName.Substring($base.Length + 1).Replace("\", "/")
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $item.FullName, $entry, "Optimal")
    }
} finally { $archive.Dispose() }
Remove-Item -Recurse -Force $stage
"{0}  ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB)
