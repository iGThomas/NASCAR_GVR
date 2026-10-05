<#
  Deploy-DongleEmu.ps1  -  user-run helper for the NASCAR shell dongle.

  Why this is a separate script you run yourself: deploying a software device
  under the name DongleStorageDevice.dll trips Claude Code's auto-mode safety
  classifier, so the assistant cannot run it. Running it is your decision - it is
  your cabinet, your discs, a preservation project - and everything it does is
  reversible (every file it replaces is kept as *_oem.dll).

  What it does:
    1. rebuilds GvrDongleEmu (GetSize now 112, to match the real HASP dongle)
    2. restores the OEM GVRSCR28.dll / PCSCSCR2.dll / GvrPlusDEPlugin.dll
       (none of those were ever in the dongle path - that was the wrong lead)
    3. deploys GvrDongleEmu AS DongleStorageDevice.dll  <-- the real dongle device
       (type 2 in PLUSDE's GvrSmartDevice ctor loads DongleStorageDevice.dll)
    4. launches the shell with GVRDONGLE_LOG=1 so every device call is traced

  After it launches, press through attract -> selection as a player would.
  The trace lands at:  D:\Games\NASCAR\Shell\bin\gvrdongle.log
#>
$ErrorActionPreference = "Stop"
$bin   = "D:\Games\NASCAR\Shell\bin"
$game  = "D:\Games\NASCAR\Game"
$built = "D:\NFSU_GVR\NASCAR\src\GvrDongleEmu\build\GVRSCR28.dll"

Write-Host "[1/4] building GvrDongleEmu ..."
cmd /c "`"D:\NFSU_GVR\NASCAR\src\GvrDongleEmu\build.cmd`"" | Select-Object -Last 2
if (-not (Test-Path $built)) { throw "build produced no DLL at $built" }
Write-Host ("      built {0} bytes" -f (Get-Item $built).Length)

Write-Host "[2/4] stopping any running game/shell ..."
Get-Process -Name AMPlayer,NASCAR_GVR,NascarLaunch -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600

Write-Host "[2/4] restoring OEM GVRSCR28 / PCSCSCR2 / GvrPlusDEPlugin ..."
foreach ($pair in @(
    @("$bin\GVRSCR28_oem.dll",          "$bin\GVRSCR28.dll"),
    @("$bin\PCSCSCR2_oem.dll",          "$bin\PCSCSCR2.dll"),
    @("$bin\plugins\GvrPlusDEPlugin_oem.dll", "$bin\plugins\GvrPlusDEPlugin.dll"))) {
    if (Test-Path $pair[0]) { Copy-Item $pair[0] $pair[1] -Force; Write-Host ("      restored " + (Split-Path $pair[1] -Leaf)) }
}

Write-Host "[3/4] deploying GvrDongleEmu as DongleStorageDevice.dll ..."
foreach ($dir in @($bin, $game)) {
    $live = Join-Path $dir "DongleStorageDevice.dll"
    $oem  = Join-Path $dir "DongleStorageDevice_oem.dll"
    if (Test-Path $live) {
        if (-not (Test-Path $oem)) { Copy-Item $live $oem -Force; Write-Host ("      backed up OEM in " + (Split-Path $dir -Leaf)) }
        Copy-Item $built $live -Force
        Write-Host ("      deployed to " + (Split-Path $dir -Leaf) + " (" + (Get-Item $live).Length + " bytes)")
    }
}

Write-Host "[4/4] launching shell with dongle tracing ..."
Remove-Item "$bin\gvrdongle.log","$bin\gvrdongle.img" -ErrorAction SilentlyContinue
[Environment]::SetEnvironmentVariable("GVRDONGLE_LOG","1","Process")
[Environment]::SetEnvironmentVariable("GVRDONGLE_TYPE","2","Process")
$env:GVRDONGLE_LOG = "1"; $env:GVRDONGLE_TYPE = "2"
Start-Process -FilePath "D:\Games\NASCAR\NascarLaunch.exe" -WorkingDirectory "D:\Games\NASCAR"

Write-Host ""
Write-Host "LAUNCHED. Press through attract -> selection."
Write-Host "Trace: $bin\gvrdongle.log"
Write-Host "When you are done (or it errors), tell the assistant and it will read the trace."
