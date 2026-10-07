<#
  Install-NASCAR-GVR-Portable.ps1

  Installs GlobalVR "NASCAR Team Racing" into a SINGLE user-chosen folder,
  with the GVR folders nested inside it, running on SQLite.

  Layout produced (for install root <ROOT>):
     <ROOT>\Game\                  the game (NASCAR_GVR.exe, GameData, Audio, LOG)
     <ROOT>\Shell\                 the Anark shell (AMPlayer.exe, *.am, bin\)
     <ROOT>\GVR\GvrPlus\           Plus schema/key/lib + game.db (the SQLite database)
     <ROOT>\GVR\Gvr_Tools\         helper tools (optional)
     <ROOT>\NascarLaunch.exe       what you start - boots the cabinet front end
     <ROOT>\nascar_settings.ini    resolution / fullscreen / cabinet-vs-race

  No MSDE / SQL Server, no C:\NASCAR, no C:\GvrPlus, no C:\hercules, no
  cabinet-lockdown Run keys, no shell replacement, no fixed C:\ anything.
  The game writes nothing to the Windows registry: its own keys live in
  <ROOT>\nascar_registry.ini (answered by the GvrIO shim), with every folder
  worked out from wherever the install sits - move or copy it and it still runs.

  Same conventions as the NFSU portable installer in
  GIT\NFSU_GVR_Portable\Install-NFSU-GVR-Portable.ps1.
#>
[CmdletBinding()]
param(
    [string]$InstallRoot = "",
    [string]$DiscPath = "",
    [string]$SourceRoot = "",
    [string]$PayloadRoot = "",
    [string]$UnshieldPath = "",
    [switch]$ExtractIfMissing,
    [string]$DependenciesRoot = "",
    [switch]$SkipShell,
    [switch]$SkipSqlite,
    [switch]$SkipGvrTools,
    [switch]$SkipFonts,
    [switch]$SkipDotNet,
    [switch]$InstallDirectX,
    [switch]$Dxvk,
    [switch]$InstallHercules,
    [switch]$SetOemRev,
    [switch]$NoShortcut,
    [switch]$Fullscreen,
    [int]$Width = 0,     # 0 = use the primary screen size (borderless fills it, sharp)
    [int]$Height = 0,
    [string]$Track = "DAYTONA",
    [string]$Series = "2006NEXTEL",
    [switch]$Uninstall,
    [switch]$NoGui,
    [switch]$DryRun,
    [switch]$ForceOverwrite
)

$ErrorActionPreference = "Stop"
$Version = "2026-10-07-nascar-portable-4.6" # 4.6: race fills the screen by default (borderless, screen size)

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrEmpty($SourceRoot)) { $SourceRoot = $Root }
$SourceRoot = $SourceRoot.TrimEnd("\")

$LogDir = Join-Path $env:TEMP "NASCAR_GVR_Install"
$LogFile = Join-Path $LogDir "install.log"

trap { try { Log ("FATAL: " + $_.Exception.Message) } catch {}; Write-Host "FAIL: $($_.Exception.Message)" -ForegroundColor Red; try { if (!$DryRun) { Show-Message ("The installation stopped:" + [Environment]::NewLine + [Environment]::NewLine + $_.Exception.Message + [Environment]::NewLine + [Environment]::NewLine + "Details: " + $LogFile) "Error" } } catch {}; exit 1 }

# GDI font registration. AddFontResource makes a newly copied font usable without a
# reboot; the WM_FONTCHANGE broadcast tells running programs to re-read the font table.
# both GVR installers define this type; a second run in the same PowerShell window must reuse it
if (-not ("GvrFontApi" -as [type])) {
Add-Type -Name GvrFontApi -Namespace "" -MemberDefinition @'
[DllImport("gdi32.dll", CharSet=CharSet.Auto)] public static extern int AddFontResource(string lpszFilename);
[DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int SendMessageTimeout(IntPtr hWnd,int Msg,IntPtr wParam,IntPtr lParam,int flags,int timeout,out IntPtr result);
'@
}

function Log($m) {
    $s = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $m
    Write-Host $s
    if (!(Test-Path $LogDir)) { New-Item -ItemType Directory -Force $LogDir | Out-Null }
    Add-Content -Path $LogFile -Value $s
}
function Warn($m) { Write-Host "[!] $m" -ForegroundColor Yellow; Add-Content -Path $LogFile -Value "WARN: $m" }
function Fail($m) { throw $m }
function New-Dir($p) { if (!(Test-Path $p)) { if ($DryRun) { Log "would create $p"; return }; New-Item -ItemType Directory -Force $p | Out-Null } }
function Test-Admin { $id = [Security.Principal.WindowsIdentity]::GetCurrent(); (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
function Assert-File($p, $label) { if (!(Test-Path $p)) { Fail "$label not found: $p" } }

# ---- the two questions as Windows dialogs (Install.bat starts PowerShell with -STA) ----------
# Windows folder dialogs need a single-threaded (STA) PowerShell. PowerShell 3+ is STA by
# default; PowerShell 2.0 (Windows 7) only with -STA. Without it the typed prompts are used.
function Test-CanShowDialogs {
    if ($NoGui) { return $false }
    if ([Threading.Thread]::CurrentThread.GetApartmentState() -ne "STA") { return $false }
    try { Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing; return $true } catch { return $false }
}
function New-DialogOwner {
    # an invisible topmost window, so the dialog opens IN FRONT of the console
    $f = New-Object System.Windows.Forms.Form
    $f.TopMost = $true; $f.ShowInTaskbar = $false; $f.Opacity = 0
    $f.StartPosition = "CenterScreen"; $f.Size = New-Object System.Drawing.Size(1, 1)
    $f.Show(); $f.Activate()
    return $f
}
# returns the chosen folder, or $null if the user cancelled
function Select-Folder($description, $startPath, [switch]$FromComputer) {
    $owner = New-DialogOwner
    try {
        $d = New-Object System.Windows.Forms.FolderBrowserDialog
        $d.Description = $description
        $d.ShowNewFolderButton = !$FromComputer
        if ($FromComputer) { $d.RootFolder = [Environment+SpecialFolder]::MyComputer }
        elseif ($startPath -and (Test-Path $startPath)) { $d.SelectedPath = $startPath }
        if ($d.ShowDialog($owner) -eq [System.Windows.Forms.DialogResult]::OK) { return $d.SelectedPath }
        return $null
    } finally { $owner.Close() }
}
function Show-Message($text, $icon = "Information") {
    if (!(Test-CanShowDialogs)) { return }
    $owner = New-DialogOwner
    try { [void][System.Windows.Forms.MessageBox]::Show($owner, $text, "NASCAR Team Racing installer", "OK", $icon) }
    finally { $owner.Close() }
}
# can we create files there? (a mounted ISO / DVD drive is read-only - seen in a Windows 7 VM)
function Test-WritableFolder($p) {
    try {
        if (!(Test-Path $p)) { New-Item -ItemType Directory -Force $p -ErrorAction Stop | Out-Null }
        $probe = Join-Path $p ("write-test-" + [Guid]::NewGuid().ToString("N") + ".tmp")
        [IO.File]::WriteAllText($probe, "x"); Remove-Item -LiteralPath $probe -Force
        return $true
    } catch { return $false }
}
# a mounted ISO or inserted disc is found without asking: data1.cab + GVRSETUP.INI saying NASCAR
function Find-NascarDisc {
    foreach ($d in @(Get-PSDrive -PSProvider FileSystem -ErrorAction SilentlyContinue)) {
        $r = $d.Root
        try {
            if (!(Test-Path (Join-Path $r "data1.cab"))) { continue }
            $ini = Join-Path $r "GVRSETUP.INI"
            if ((Test-Path $ini) -and ([IO.File]::ReadAllText($ini) -match '(?m)^OSName=NASCAR')) { return $r }
        } catch { }
    }
    return $null
}

function Copy-Contents($s, $d, $label) {
    if (!(Test-Path $s)) { Warn "$label source missing, skipped: $s"; return }
    if ($DryRun) { Log "would copy $label -> $d"; return }
    Log "copying $label -> $d"
    New-Dir $d
    Copy-Item -Path (Join-Path $s "*") -Destination $d -Recurse -Force
}

function Copy-FileIfPresent($s, $d, $label) {
    if (!(Test-Path $s)) { Warn "$label not present: $s"; return }
    if ($DryRun) { Log "would copy $label -> $d"; return }
    New-Dir (Split-Path -Parent $d)
    Copy-Item -LiteralPath $s -Destination $d -Force
}

# Older installs wrote the game's keys to HKLM (both views on x64); the uninstall still
# clears them. New installs keep them in nascar_registry.ini instead (Install-Registry).
function Remove-Reg($subKey) {
    $paths = @("HKLM:\SOFTWARE\$subKey")
    if ([IntPtr]::Size -eq 8) { $paths += "HKLM:\SOFTWARE\WOW6432Node\$subKey" }
    foreach ($p in $paths) {
        if (Test-Path $p) {
            if ($DryRun) { Log "would remove $p"; continue }
            Remove-Item -Path $p -Recurse -Force; Log "  removed $p"
        }
    }
}

# ===================== payload ============================================

$DiscRoot = Join-Path $SourceRoot "GAME ISO EXTRACTED"
$ExtractRoot = Join-Path $SourceRoot "Extracted\Disc"
# -DiscPath: the game disc (a drive letter, a mounted ISO or a copied folder holding
# data1.cab). The cabinets are unpacked into %TEMP% and the payload taken from there.
if (![string]::IsNullOrEmpty($DiscPath)) {
    $DiscRoot = $DiscPath.TrimEnd("\")
    if ($DiscRoot -match '^[A-Za-z]:$') { $DiscRoot += "\" }
    $ExtractRoot = Join-Path $env:TEMP "NASCAR_GVR_Extract"
    $ExtractIfMissing = $true
}
if ([string]::IsNullOrEmpty($PayloadRoot)) { $PayloadRoot = Join-Path $ExtractRoot "File_Group" }
$PayloadRoot = $PayloadRoot.TrimEnd("\")

function Resolve-Unshield {
    if (![string]::IsNullOrEmpty($UnshieldPath)) { Assert-File $UnshieldPath "unshield.exe"; return $UnshieldPath }
    foreach ($c in @((Join-Path $SourceRoot "Tools\unshield.exe"),
                     (Join-Path (Split-Path -Parent $SourceRoot) "release\Tools\unshield.exe"))) {
        if (Test-Path $c) { return $c }
    }
    Fail "unshield.exe not found. Build it with Build-Unshield.ps1 or pass -UnshieldPath."
}

function Extract-Payload {
    Assert-File (Join-Path $DiscRoot "data1.cab") "data1.cab (game disc)"
    $ex = Resolve-Unshield
    Log "extracting InstallShield cabinets (~800 MB, a few minutes) with $ex"
    if ($DryRun) { return }
    New-Dir $ExtractRoot
    $prev = Get-Location
    try { Set-Location -LiteralPath $DiscRoot; & $ex -d $ExtractRoot x "data1.cab" | Out-Null }
    finally { Set-Location -LiteralPath $prev }
    if (!(Test-Path (Join-Path $PayloadRoot "NASCAR\Game\NASCAR_GVR.exe"))) { Fail "extraction did not produce NASCAR_GVR.exe" }
    # unshield turns spaces in DIRECTORY names into underscores; the game has seven of those
    # names compiled in and silently crashes without them. (Its -R switch would also rename
    # its own "File_Group" folder, so the names are repaired here instead.)
    $fix = Join-Path $SourceRoot "Tools\Fix-GvrDirNames.ps1"
    if (Test-Path $fix) { & $fix -Root (Join-Path $PayloadRoot "NASCAR") | Out-Null }
    if (!(Test-Path -LiteralPath (Join-Path $PayloadRoot "NASCAR\Game\Options\Basic Settings"))) { Fail "could not restore the directory names after extraction" }
}

# ===================== install steps ======================================

# GvrIO.dll is a static import of NASCAR_GVR.exe but ships in Shell\bin. The OEM
# put C:\NASCAR\SHELL\BIN on the machine PATH; we copy the DLLs next to the game
# instead, so nothing global is touched.
$GvrIoDlls = @("GvrIO.dll", "USBIOExtreme.dll", "GvrParseXml.dll", "GvrSound.dll",
               "GvrLinking.dll", "Dongle.dll", "Steering.dll", "Motion.dll")
# MSVC 7.1 runtimes the 2003-era binaries link against; app-local, never system32.
# msvcr71d/msvcp71d (debug builds): without them NASCARPlugIn.dll silently fails to load.
$LegacyDlls = @("msvcr71.dll", "msvcp71.dll", "MFC71.dll", "msvcr70.dll", "msvcr71d.dll", "msvcp71d.dll")

# The GVR device layer, from the disc's WINDOWS\system32 file group. PLUSDE.dll
# loads "DongleStorageDevice.dll" BY NAME (the string is in PLUSDE) and calls its
# CreateGVRStorageDeviceImp export, via GVRStorageDevice.dll - the smart-card /
# dongle storage ABI documented in the parent project's GVRSCR28_ABI_REPORT.md.
# Without these the Anark shell dies a second or two after its database init with
# an unhandled exception (0xC000041D) inside GvrPlusDEPlugin.dll. Verified: with
# them present the shell stays up indefinitely.
# GVRSDEmulator.dll is GlobalVR's own SOFTWARE emulator of the same ABI and is
# staged alongside, so the device can be emulated instead of requiring hardware.
$DeviceDlls = @(
    "GVRStorageDevice.dll", "DongleStorageDevice.dll", "GVRSDEmulator.dll",
    "GVRSCR28.dll", "PCSCSCR2.dll", "GVR_Resources.dll", "Resources.dll",
    "UsbTrackerDll.dll", "GVRInputRaw.dll", "akshasp.dll", "haspms32.dll",
    "csamsp.dll", "MdmXSdk.dll"
)

function Install-Payload($game, $shell, $gvr) {
    Log "--- game + shell ---"
    Copy-Contents (Join-Path $PayloadRoot "NASCAR\Game") $game "game tree"
    # Config.ini says LOGDIR=LOG\ but no LOG folder ships and the engine will not
    # create one - without this the game writes no log at all.
    New-Dir (Join-Path $game "LOG")
    if (!$SkipShell) { Copy-Contents (Join-Path $PayloadRoot "NASCAR\Shell") $shell "Anark shell tree" }


    if (!$SkipGvrTools) {
        Copy-Contents (Join-Path $PayloadRoot "GVR\GVR_Tools") (Join-Path $gvr "Gvr_Tools") "GVR helper tools"
    }
    if ($InstallHercules) {
        Warn "-InstallHercules: staging the cabinet monitors as FILES ONLY; nothing is autostarted."
        Copy-Contents (Join-Path $PayloadRoot "Hercules") (Join-Path $gvr "Hercules") "Hercules monitors"
    }
    # OEM .reg files kept purely as reference material.
    Copy-Contents (Join-Path $PayloadRoot "NASCAR\InstallGVR") (Join-Path $gvr "OEM_Reference") "OEM registry reference"
}

# Seven of the game's directories are named with SPACES, and unshield's default
# extraction turns spaces in directory names into underscores (file names are
# left alone). NASCAR_GVR.exe carries those names as string literals, so the
# mangling is fatal rather than cosmetic: during Game::CreateManagers the engine
# enumerates "Basic Settings\*.CTL", and when the search comes back empty it
# stores NULL as the current entry, asks the path resolver for its full path
# anyway (which returns NULL) and strcpy()s from it - an access violation at
# 0x0048B990. The game's unhandled-exception filter logs to a FILE* the release
# build never opens and then _exit(-1)s, so the process just disappears with no
# log, no dialog and no WER event.
#
# The extractor now repairs this itself, so this is a safety net for payloads
# unpacked before the fix; it is idempotent and cheap.
function Fix-DirNames($game) {
    $probe = Join-Path $game "Options\Basic Settings"
    if (Test-Path -LiteralPath $probe) { Log "directory names already correct"; return }

    # $SourceRoot, not $PSScriptRoot: PowerShell 2.0 (Windows 7) leaves $PSScriptRoot empty in scripts
    $fix = Join-Path $SourceRoot "Tools\Fix-GvrDirNames.ps1"
    if (!(Test-Path $fix)) { Warn "Tools\Fix-GvrDirNames.ps1 missing - the game will crash on startup."; return }
    if ($DryRun) { Log "would normalise mangled directory names under $game"; return }

    Log "--- restoring directory names mangled by extraction ---"
    & $fix -Root $game | ForEach-Object { if ($_ -match '^\s*renamed') { Log ("  " + $_.Trim()) } }
    if (!(Test-Path -LiteralPath $probe)) { Fail "Could not restore '$probe' - the game cannot start without it." }
}

# The shell loads the plug-ins listed in <GameRoot>\config\GvrShellPlugInList.xml.
# Two of them are cabinet hardware/network features this install does not have,
# and GvrLinkingPlugIn is FATAL to a standalone install: it Sleep-polls the
# cabinet-to-cabinet linking layer (GvrLinking.dll - IsOnline/GetCabsAvailable over
# the UDP ports gvrNetworkConfig.bat opens) and the front end never presents its
# window until that resolves. Symptom: shell alive, 100% CPU, a correctly sized
# fullscreen window that is never shown. Proven by sampling the hot thread
# (parked in NtDelayExecution) and walking its stack to GvrLinkingPlugIn.dll.
# Removing it drops CPU from ~100% to ~4% and the attract screen appears.
# GvrMotionPlugIn goes too - it drives the Tsunami motion base (see tsunami.reg).
# The OEM file is kept as GvrShellPlugInList.xml.oem.
function Disable-CabinetPlugIns($game) {
    $cfg = Join-Path $game "config\GvrShellPlugInList.xml"
    if (!(Test-Path $cfg)) { Warn "no GvrShellPlugInList.xml; skipping plug-in trim"; return }
    Log "--- trimming cabinet-only shell plug-ins ---"
    $text = [IO.File]::ReadAllText($cfg)
    $orig = $text
    foreach ($tag in @("GvrLinkingPlugIn", "GvrMotionPlugIn")) {
        $text = $text -replace ("(?m)^\s*<" + $tag + "\s*/>\s*$eol?"), ""
        $text = $text.Replace("<$tag />", "")
    }
    # drop the blank lines the removals leave behind, so the result matches the
    # file shape this was validated against
    $text = (($text -split "`r?`n" | Where-Object { $_.Trim() -ne "" }) -join "`r`n") + "`r`n"
    if ($text -eq $orig) { Log "  already trimmed"; return }
    if ($DryRun) { Log "  would remove GvrLinkingPlugIn + GvrMotionPlugIn from $cfg"; return }
    $bak = "$cfg.oem"
    if (!(Test-Path $bak)) { Copy-Item -LiteralPath $cfg -Destination $bak -Force }
    [IO.File]::WriteAllText($cfg, $text)
    Log "  removed GvrLinkingPlugIn + GvrMotionPlugIn (OEM kept as .oem)"
}

# NASCAR_GVR.exe constructs a cGvrIO and destroys it again WITHOUT ever calling
# Initialize. The OEM destructor then waits forever for a worker-thread heartbeat
# that only Initialize starts (GvrIO.dll RVA 0x2570 spins on the flag at
# 0x1000C02C), so the game hangs during startup at ~15 MB and 0% CPU - it never
# reaches graphics or even its dongle check. Forcing that wait open is not enough:
# the same teardown then deletes an uninitialised critical section and frees
# globals that were never allocated, corrupting the heap.
#
# src\GvrIOShim is a drop-in GvrIO.dll: 9 of the 11 exports are plain forwarders to
# GvrIO_oem.dll, so OEM code runs untouched; the destructor and Initialize are
# overridden, and a watchdog supplies the missing heartbeat only after it has been
# absent for 120 ms (so a real worker always wins). It ALSO stubs the game's HASP
# dongle gate: its DllMain runs before NASCAR_GVR.exe's main(), so 39 bytes over
# FUN_00672460 satisfy the check without the executable ever being modified on
# disk (GVRIOSHIM_NO_DONGLE_PATCH=1 opts out for a cabinet with a real dongle).
# Build it with src\GvrIOShim\build.cmd. Set GVRIOSHIM_LOG=1 for a trace.
#
# Deployed to the game AND the shell (Shell\bin): besides the race fixes it carries the
# game's private registry (nascar_registry.ini, NASCAR_FINDINGS section 15), the shell's
# IO-board / pedal answers and the Xbox / PlayStation pad support - both programs need it.
function Install-GvrIoShim($game, $shell) {
    $shim = Join-Path $SourceRoot "src\GvrIOShim\build\GvrIO.dll"
    if (!(Test-Path $shim)) {
        Warn "GvrIO shim not built (src\GvrIOShim\build.cmd) - the game will hang on startup."
        return
    }
    Log "--- GvrIO shim (startup deadlock, dongle gate, private registry, gamepads) ---"
    foreach ($dir in @($game, (Join-Path $shell "bin"))) {
        $live = Join-Path $dir "GvrIO.dll"
        $oem  = Join-Path $dir "GvrIO_oem.dll"
        if ($DryRun) { Log "  would install the shim as $live and keep the OEM as $oem"; continue }
        # only treat the current file as OEM if it is not already our shim
        if ((Test-Path $live) -and !(Test-Path $oem)) { Copy-Item -LiteralPath $live -Destination $oem -Force }
        Copy-FileIfPresent $shim $live "GvrIO.dll (shim)" | Out-Null
        Log "  $dir : OEM kept as GvrIO_oem.dll"
    }
}

# Support DLLs are staged on EVERY run (idempotent), not just during the first
# copy, so re-running the installer repairs or adds them to an existing install -
# the same reason the NFSU portable installer stages its DLLs unconditionally.
function Install-SupportDlls($game, $shell) {
    Log "--- support DLLs (app-local, never system32) ---"
    $binSrc = Join-Path $PayloadRoot "NASCAR\Shell\bin"
    $sysSrc = Join-Path $PayloadRoot "WINDOWS\system32"
    foreach ($d in $GvrIoDlls) { Copy-FileIfPresent (Join-Path $binSrc $d) (Join-Path $game $d) $d }
    foreach ($set in @($LegacyDlls, $DeviceDlls)) {
        foreach ($d in $set) {
            Copy-FileIfPresent (Join-Path $sysSrc $d) (Join-Path $game $d) $d
            if (!$SkipShell) { Copy-FileIfPresent (Join-Path $sysSrc $d) (Join-Path $shell "bin\$d") $d }
        }
    }
}

# The operator-menu XML files carry absolute C:\NASCAR paths, which is what would
# break a relocated install (the NASCAR equivalent of the NFSU GVRD path repoint).
# 5 more point at C:\NASCAR_TOUR_GOLF_2006 - dead leftovers from the PGA Golf
# codebase this shell was derived from, whose target .am files DO exist here, so
# repointing them fixes menu entries that were broken on the real cabinet too.
function Repoint-ShellPaths($shell) {
    if ($SkipShell) { return }
    $menuDir = Join-Path $shell "OPERATOR_MENUS"
    if ($DryRun -and !(Test-Path $menuDir)) {
        # nothing was really copied, so preview against the payload instead
        $menuDir = Join-Path $PayloadRoot "NASCAR\Shell\OPERATOR_MENUS"
    }
    if (!(Test-Path $menuDir)) { Warn "no OPERATOR_MENUS folder; skipping path repoint"; return }
    Log "--- repointing operator-menu paths at $shell ---"
    $files = @(Get-ChildItem -Recurse $menuDir -Include *.xml -ErrorAction SilentlyContinue | Where-Object { !$_.PSIsContainer })
    $changed = 0
    foreach ($f in $files) {
        $t = [IO.File]::ReadAllText($f.FullName)
        $o = $t
        $t = $t -replace '(?i)C:\\NASCAR_TOUR_GOLF_2006\\Shell\\', ($shell.TrimEnd('\') + '\')
        $t = $t -replace '(?i)C:\\NASCAR\\Shell\\', ($shell.TrimEnd('\') + '\')
        if ($t -ne $o) {
            if ($DryRun) { Log "  would repoint $($f.Name)" }
            else { [IO.File]::WriteAllText($f.FullName, $t) }
            $changed++
        }
    }
    Log "  repointed $changed operator-menu file(s)"
}

# AMPlayer.exe is a NATIVE host that loads the mixed-mode PLUSDE.dll and
# GvrPlusDEPlugin.dll. With no .config the CLR shim binds the NEWEST installed
# runtime - v2.0.50727 on Windows 10/11 - and PLUSDE's native C++ exception
# interop access-violates there. Verified in a Process Monitor trace: the shim
# probed for AMPlayer.exe.config, did not find it, loaded v2.0.50727\mscorwks.dll
# and the 2.0 System.Data, and the shell died with 0xC0000005 in JIT'd memory
# right after its first database query. With this file it binds v1.1.4322 and
# runs the whole cabinet initialisation instead.
function Install-ClrPin($shell) {
    if ($SkipShell) { return }
    Log "--- pinning the shell to CLR 1.1 ---"
    $cfg = Join-Path $shell "bin\AMPlayer.exe.config"
    $xml = @(
        '<?xml version="1.0"?>',
        '<!--',
        '  Pins the Anark shell to the .NET 1.1 runtime.',
        '  Without this the CLR shim loads the newest runtime (2.0 on Win10/11) and',
        '  PLUSDE''s native C++ exception interop access-violates, killing the shell a',
        '  few seconds in, right after its first database query.',
        '-->',
        '<configuration>',
        '  <startup>',
        '    <requiredRuntime version="v1.1.4322" safemode="true"/>',
        '    <supportedRuntime version="v1.1.4322"/>',
        '  </startup>',
        '</configuration>'
    )
    if ($DryRun) { Log "  would write $cfg"; return }
    New-Dir (Split-Path -Parent $cfg)
    Set-Content -Path $cfg -Value ($xml -join "`r`n") -Encoding ASCII
    Log "  wrote $cfg"
}

function Install-Sqlite($game, $shell, $plus, $dbPath) {
    if ($SkipSqlite) { Log "SQLite backend skipped (-SkipSqlite): the shell will expect SQL Server"; return }
    Log "--- SQLite database backend ---"
    $deploy = Join-Path $SourceRoot "Deploy"
    foreach ($f in @("PLUSDE.dll", "GvrSqlite.dll", "sqlite3.dll", "game.db")) {
        if (!(Test-Path (Join-Path $deploy $f))) {
            Fail "SQLite backend not built: missing $f. Run Tools\Build-SqliteBackend.ps1 first."
        }
    }

    Copy-Contents (Join-Path $PayloadRoot "GVRPLUS") $plus "GvrPlus payload"

    # Prefer compiling the provider on THIS machine with its own .NET 1.1 compiler -
    # a locally built assembly can never be a version mismatch for the local runtime.
    # The prebuilt Deploy\GvrSqlite.dll is the fallback.
    $csc11 = "C:\Windows\Microsoft.NET\Framework\v1.1.4322\csc.exe"
    $src = Join-Path $SourceRoot "src\GvrSqlite\GvrSqlite.cs"
    if (!(Test-Path $src)) { $src = Join-Path (Split-Path -Parent $SourceRoot) "GvrSqlite\GvrSqlite.cs" }
    $provider = Join-Path $deploy "GvrSqlite.dll"
    if ((Test-Path $csc11) -and (Test-Path $src) -and !$DryRun) {
        $out = Join-Path $LogDir "GvrSqlite.dll"
        if (Test-Path $out) { Remove-Item -LiteralPath $out -Force }
        & $csc11 /nologo /target:library /out:$out $src 2>&1 |
            Where-Object { $_ -notmatch '^\s*$' -and $_ -notmatch 'warning CS' } | ForEach-Object { Log "    $_" }
        if (Test-Path $out) { $provider = $out; Log "  provider compiled on this machine (.NET 1.1 csc)" }
        else { Warn "  local compile failed - using the prebuilt provider" }
    }

    # PLUSDE.dll is not strong-named and is found by plain DLL search order, so a
    # copy beside each consuming executable is what makes the swap take effect.
    $dests = @($game)
    if (!$SkipShell) { $dests += (Join-Path $shell "bin") }
    foreach ($d in $dests) {
        foreach ($f in @("PLUSDE.dll", "GvrSqlite.dll", "sqlite3.dll")) {
            $target = Join-Path $d $f
            if ((Test-Path $target) -and $f -eq "PLUSDE.dll" -and !(Test-Path "$target.oem") -and !$DryRun) {
                Copy-Item -LiteralPath $target -Destination "$target.oem" -Force
            }
            $from = if ($f -eq "GvrSqlite.dll") { $provider } else { Join-Path $deploy $f }
            Copy-FileIfPresent $from $target $f
        }
    }

    # The database holds leaderboards and operator settings - never clobbered.
    New-Dir (Split-Path -Parent $dbPath)
    if ((Test-Path $dbPath) -and !$ForceOverwrite) {
        Warn "existing database kept: $dbPath (use -ForceOverwrite to reset it)"
    }
    else { Copy-FileIfPresent (Join-Path $deploy "game.db") $dbPath "game.db" }

    # No machine-wide variable: NascarLaunch.exe hands the shell and the race THIS install's
    # game.db, and the provider can also derive it from the private PlusSchemaPath - so two
    # installs never share a database and a moved install keeps working.
}

# ===================== OEM file patches ===================================
# Patches\nascar-bytepatches.txt (made by Tools\Make-BytePatches.py) replaces bytes IN PLACE
# in OEM files - no file changes size. Per file, every patch is checked against the OEM bytes
# first; a file that matches neither the OEM nor the patched bytes is left alone (a different
# build must never be half-patched). The OEM copy is kept as <name>.oem (not .dll, so the
# shell can never load it as a plug-in). What the patches do (NASCAR_FINDINGS.md):
#   Shell\bin\Dongle.dll              - the shell's dongle self-test reads a valid image (s.13)
#   PlugIns\NASCARPlugIn.dll          - never sets the Windows time zone, does not treat D:\ as
#                                       the CD drive, no receive timeout (s.11.5)
#   PlugIns\HerculesPlugIn.dll        - no wait for the cabinet monitor processes
#   Shell.am                          - 15 s IO-board wait at startup -> 0.5 s
#                                       and no "rundll32 nvcpl.dll,dtcfg setmode" calls (a RunDLL
#                                       error box on PCs without a 32-bit NVIDIA nvcpl.dll)
#   NASCAR_Selection/Attract.am, the operator-menu scenes
#                                     - cabinet C:\NASCAR texture/audio paths made relative and
#                                       the SendPacket calls to the missing link board removed
function Apply-BytePatches($root) {
    if ($SkipShell) { return }
    $table = Join-Path $SourceRoot "Patches\nascar-bytepatches.txt"
    if (!(Test-Path $table)) { Fail "patch table missing: $table" }
    Log "--- OEM file patches ---"
    $files = New-Object System.Collections.Specialized.OrderedDictionary; $cur = $null   # [ordered] needs PowerShell 3
    foreach ($line in [IO.File]::ReadAllLines($table)) {
        if ($line -match '^\s*(#|$)') { continue }
        if ($line -match '^file (.+)$') { $cur = $Matches[1].Trim(); $files[$cur] = New-Object System.Collections.ArrayList; continue }
        $p = $line.Split(' ')
        [void]$files[$cur].Add(@([Convert]::ToInt32($p[0], 16), $p[1], $p[2]))
    }
    function HexBytes($h) { $b = New-Object byte[] ($h.Length / 2); for ($i = 0; $i -lt $b.Length; $i++) { $b[$i] = [Convert]::ToByte($h.Substring($i * 2, 2), 16) }; return ,$b }
    function Matches($data, $off, $bytes) {
        if ($off + $bytes.Length -gt $data.Length) { return $false }
        for ($i = 0; $i -lt $bytes.Length; $i++) { if ($data[$off + $i] -ne $bytes[$i]) { return $false } }
        return $true
    }
    $done = 0; $already = 0
    foreach ($rel in $files.Keys) {
        $path = Join-Path $root $rel
        if (!(Test-Path -LiteralPath $path)) { if ($DryRun) { Log "  would patch $rel" } else { Warn "  $rel not found - not patched" }; continue }
        $data = [IO.File]::ReadAllBytes($path)
        $isOem = $true; $isNew = $true
        foreach ($p in $files[$rel]) {
            if (!(Matches $data $p[0] (HexBytes $p[1]))) { $isOem = $false }
            if (!(Matches $data $p[0] (HexBytes $p[2]))) { $isNew = $false }
        }
        if ($isNew) { $already++; continue }
        if (!$isOem -and (Test-Path -LiteralPath "$path.oem")) {
            # an older release's patch: start again from the OEM copy kept beside it
            $orig = [IO.File]::ReadAllBytes("$path.oem"); $origOk = $true
            foreach ($p in $files[$rel]) { if (!(Matches $orig $p[0] (HexBytes $p[1]))) { $origOk = $false } }
            if ($origOk) { $data = $orig; $isOem = $true; Log "  $rel : updating an older patch from $rel.oem" }
        }
        if (!$isOem) { Warn "  $rel is not the expected OEM file - left unpatched"; continue }
        if ($DryRun) { Log "  would patch $rel ($($files[$rel].Count) change(s))"; continue }
        $bak = "$path.oem"
        if (!(Test-Path -LiteralPath $bak)) { [IO.File]::WriteAllBytes($bak, $data) }
        foreach ($p in $files[$rel]) { $b = HexBytes $p[2]; [Array]::Copy($b, 0, $data, $p[0], $b.Length) }
        [IO.File]::WriteAllBytes($path, $data)
        $done++
    }
    Log "  patched $done file(s), $already already patched (OEM copies kept as *.oem)"
}

# Two operator-menu pages ask the cabinet for its network card address and the operator's
# contact details; on a PC those calls never answer and the page hangs. Static text instead.
function Edit-OperatorXml($shell) {
    if ($SkipShell) { return }
    $dir = Join-Path $shell "OPERATOR_MENUS\XML"
    $edits = @(
        @{ f = "MachineInfoCol.xml"
           re = '(?s)<DYNAMIC_TEXT>\s*<LOGIC>\s*<GETPROPERTY>MacAddress</GETPROPERTY>\s*</LOGIC>\s*</DYNAMIC_TEXT>'
           to = '<STATIC_TEXT><VALUE>N/A</VALUE></STATIC_TEXT>' },
        @{ f = "SysStatusCol.xml"
           re = '(?s)<DYNAMIC_TEXT>\s*<LOGIC>\s*<CALL METHOD="NASCAR_GET_CONTACT_HEADER">\s*<PARAM>\d+</PARAM>\s*</CALL>\s*</LOGIC>\s*</DYNAMIC_TEXT>'
           to = '<STATIC_TEXT><VALUE> </VALUE></STATIC_TEXT>' }
    )
    foreach ($e in $edits) {
        $p = Join-Path $dir $e.f
        if (!(Test-Path $p)) { if (!$DryRun) { Warn "  $($e.f) not found" }; continue }
        $t = [IO.File]::ReadAllText($p)
        $n = ([regex]::Matches($t, $e.re)).Count
        if ($n -eq 0) { Log "  $($e.f): already edited"; continue }
        if ($DryRun) { Log "  would replace $n cabinet-only cell(s) in $($e.f)"; continue }
        if (!(Test-Path "$p.oem")) { Copy-Item -LiteralPath $p -Destination "$p.oem" }
        [IO.File]::WriteAllText($p, [regex]::Replace($t, $e.re, $e.to))
        Log "  $($e.f): $n cabinet-only cell(s) -> static text"
    }
}

# GvrIO.xml maps keyboard keys to cabinet buttons:
#  + J = NOS, the horn (the gamepad's horn button presses it; H is the engine's HUD hotkey)
#  - A = MotionDisable, which pops up the motion-seat panel in the middle of a race
function Edit-GvrIoXml($game) {
    $p = Join-Path $game "config\GvrIO.xml"
    if (!(Test-Path $p)) { if (!$DryRun) { Warn "  GvrIO.xml not found" }; return }
    $t = [IO.File]::ReadAllText($p); $o = $t
    $nos = '<key char="j" name="NOS" mode="Default" messageID="GVRIO_NOS_PRESSED" event="onGvrNOSPressed" />'
    if ($t -notmatch 'name="NOS"') {
        $t = ([regex]'(?m)^(\s*)(<key char="s" name="Start"[^\r\n]*)').Replace($t, ('$1' + $nos + "`r`n" + '$1$2'), 1)
    }
    $t = [regex]::Replace($t, '(?m)^[ \t]*<key char="a" name="MotionDisable"[^\r\n]*\r?\n', '')
    if ($t -eq $o) { Log "  GvrIO.xml: already edited"; return }
    if ($DryRun) { Log "  would add J = horn and remove A = motion panel in GvrIO.xml"; return }
    if (!(Test-Path "$p.oem")) { Copy-Item -LiteralPath $p -Destination "$p.oem" }
    [IO.File]::WriteAllText($p, $t)
    Log "  GvrIO.xml: J = horn added, A = motion-seat panel removed"
}

# The shell's startup script runs Shell\GammaSet.exe, which calls the NVIDIA control panel
# (NvCpl.dll) to set cabinet gamma/vibrance - an error box on most PCs, and a changed desktop
# on the ones where it works. Renamed so it cannot run.
function Disable-GammaSet($shell) {
    if ($SkipShell) { return }
    $exe = Join-Path $shell "GammaSet.exe"
    if (!(Test-Path $exe)) { return }
    if ($DryRun) { Log "would rename GammaSet.exe -> GammaSet.exe.arcade-disabled"; return }
    $dst = "$exe.arcade-disabled"
    # [IO.File]::Move, not Rename-Item -LiteralPath (PowerShell 3+)
    if (Test-Path $dst) { Remove-Item -LiteralPath $exe -Force } else { [IO.File]::Move($exe, $dst) }
    Log "  GammaSet.exe disabled (cabinet gamma/vibrance tool)"
}

# Dependencies are large (100 MB DirectX redist, 35 MB .NET 1.1); reuse the ones the
# NFSU portable package already carries rather than duplicating them here.
function Resolve-Dependencies {
    if (![string]::IsNullOrEmpty($DependenciesRoot)) { return $DependenciesRoot }
    foreach ($c in @((Join-Path $SourceRoot "Dependencies"),
                     (Join-Path (Split-Path -Parent $SourceRoot) "GIT\NFSU_GVR_Portable\Dependencies"))) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

# The shell + PLUSDE need REAL CLR 1.1. Do not redirect them to CLR 2.0: PLUSDE's
# mixed-mode code uses native C++ exceptions as control flow and that EH interop
# access-violates on 2.0 (proven in the NFSU work). The plain 1.1 redist fails with
# MSI 1603 on Windows 10/11 - the fix is slipstreaming SP1 into an admin image.
function Ensure-DotNet {
    if ($SkipDotNet -or $SkipShell) { return }
    if (Test-Path (Join-Path $env:WINDIR "Microsoft.NET\Framework\v1.1.4322\mscorlib.dll")) { Log ".NET 1.1 present"; return }
    $dep = Resolve-Dependencies
    $dfx = if ($dep) { Join-Path $dep "DotNet11\dotnetfx.exe" } else { $null }
    $sp1 = if ($dep) { Join-Path $dep "DotNet11\NDP1.1sp1-KB867460-X86.exe" } else { $null }
    if (!$dfx -or !(Test-Path $dfx)) {
        Warn ".NET 1.1 is missing and the redist is not bundled. The game itself does not need it,"
        Warn "but the Anark shell + PLUSDE (the database layer) do. Install it, or use -SkipShell."
        return
    }
    if ($DryRun) { Log "would install .NET 1.1 SP1 (slipstreamed)"; return }
    if (!(Test-Path $sp1)) { Fail "NDP1.1sp1-KB867460-X86.exe is required on Windows 10/11 (plain redist = MSI 1603)" }

    Log "installing .NET 1.1 + SP1 (slipstreamed - required on Windows 10/11)"
    $w = Join-Path $LogDir "dotnet11"; $adm = Join-Path $LogDir "dotnet11_admin"
    New-Dir $w
    Start-Process $dfx -ArgumentList "/q /c /t:`"$w`"" -Wait | Out-Null
    if (!(Test-Path (Join-Path $w "netfx.msi"))) { Fail "dotnetfx.exe extraction failed" }
    Start-Process $sp1 -ArgumentList "/Xp:`"$w\netfxsp.msp`"" -Wait | Out-Null
    Start-Process msiexec -ArgumentList "/a `"$w\netfx.msi`" TARGETDIR=`"$adm`" /qn" -Wait | Out-Null
    Start-Process msiexec -ArgumentList "/a `"$adm\netfx.msi`" /p `"$w\netfxsp.msp`" /qn" -Wait | Out-Null
    $p = Start-Process msiexec -ArgumentList "/i `"$adm\netfx.msi`" /qn /norestart" -Wait -PassThru
    if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { Fail ".NET 1.1 SP1 install failed ($($p.ExitCode))" }
    Log "  .NET 1.1 SP1 installed"
}

# NASCAR_GVR.exe imports d3d9 directly and no d3dx9 helper, so the June-2010 redist
# is normally unnecessary - unlike NFSU, which needs it. Opt in with -InstallDirectX.
function Ensure-DirectX {
    if (!$InstallDirectX) { return }
    $dep = Resolve-Dependencies
    $redist = if ($dep) { Join-Path $dep "DirectX\directx_Jun2010_redist.exe" } else { $null }
    if (!$redist -or !(Test-Path $redist)) { Warn "DirectX June-2010 redist not bundled; skipping"; return }
    if ($DryRun) { Log "would install DirectX 9 (June 2010 redist)"; return }
    Log "installing DirectX 9 (June 2010 redist)"
    $tmp = Join-Path $LogDir "dx"; New-Dir $tmp
    Start-Process $redist -ArgumentList @("/Q", "/T:$tmp", "/C") -Wait | Out-Null
    $dxs = Join-Path $tmp "DXSETUP.exe"
    if (Test-Path $dxs) { Start-Process $dxs -ArgumentList "/silent" -Wait | Out-Null; Log "  DirectX 9 installed" }
    else { Warn "DXSETUP.exe not found after extracting the redist" }
}

# App-local DXVK (D3D9 -> Vulkan) beside the game. In the NFSU work this was needed
# because that engine's physics is framerate-tied and ran absurdly fast on modern GPUs.
# Whether NASCAR's EA engine has the same tie is UNVERIFIED (it is a different engine
# and the cabinet ran 1360x768 on a fixed-refresh panel), so this is opt-in: use -Dxvk
# if the game runs too fast or the native D3D9 path misbehaves on your GPU.
function Deploy-Dxvk($game) {
    if (!$Dxvk) { return }
    $dep = Resolve-Dependencies
    $dll = if ($dep) { Join-Path $dep "DXVK\d3d9.dll" } else { $null }
    if (!$dll -or !(Test-Path $dll)) { Warn "DXVK d3d9.dll not bundled; skipping"; return }
    if (!(Test-Path (Join-Path $env:WINDIR "System32\vulkan-1.dll"))) { Log "no Vulkan runtime - skipping DXVK"; return }
    if ($DryRun) { Log "would deploy DXVK d3d9.dll + dxvk.conf (60 fps cap) next to NASCAR_GVR.exe"; return }
    Copy-Item $dll (Join-Path $game "d3d9.dll") -Force
    [IO.File]::WriteAllText((Join-Path $game "dxvk.conf"),
        "# cap to 60fps - the cabinet ran a fixed-refresh panel.`r`n# Remove this line if NASCAR's physics turns out not to be framerate-tied.`r`nd3d9.maxFrameRate = 60`r`n")
    Log "  DXVK d3d9.dll + dxvk.conf deployed (60 fps cap)"
}

# Registers ONLY the families Windows does not already have, and keeps copies in
# <ROOT>\Fonts. The disc also carries stock Microsoft faces (Courier New et al.);
# dropping those over the system family is a good way to break text everywhere.
function Install-Fonts($root) {
    if ($SkipFonts) { return }
    $src = Join-Path $PayloadRoot "WINDOWS\Fonts"
    if (!(Test-Path $src)) { return }
    Log "--- fonts ---"
    $files = @(Get-ChildItem -LiteralPath $src -ErrorAction SilentlyContinue |
               Where-Object { !$_.PSIsContainer -and $_.Extension -match '(?i)^\.(ttf|otf|ttc|fon)$' })
    if ($DryRun) { Log "  would install the missing families from $($files.Count) font file(s)"; return }

    $dst = Join-Path $root "Fonts"
    New-Dir $dst
    foreach ($f in $files) { Copy-Item $f.FullName (Join-Path $dst $f.Name) -Force }
    Log "  kept $($files.Count) font file(s) in $dst"

    try { Add-Type -AssemblyName System.Drawing -ErrorAction Stop } catch { Warn "  System.Drawing unavailable - skipping registration"; return }
    $installed = (New-Object System.Drawing.Text.InstalledFontCollection).Families | ForEach-Object { $_.Name }
    $winFonts = Join-Path $env:windir "Fonts"
    $key = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts"
    $added = 0; $skipped = 0
    foreach ($f in $files) {
        $fam = $null
        try { $pfc = New-Object System.Drawing.Text.PrivateFontCollection; $pfc.AddFontFile($f.FullName); $fam = $pfc.Families[0].Name } catch { }
        if (!$fam) { Warn "  cannot read the family name of $($f.Name) - skipped"; continue }
        if ($installed -contains $fam) { $skipped++; continue }
        try {
            Copy-Item $f.FullName (Join-Path $winFonts $f.Name) -Force
            New-ItemProperty -Path $key -Name "$fam (TrueType)" -Value $f.Name -PropertyType String -Force | Out-Null
            [void][GvrFontApi]::AddFontResource((Join-Path $winFonts $f.Name))
            Log "  installed $fam ($($f.Name))"
            $added++
        }
        catch { Warn "  could not install $fam - $_" }
    }
    if ($added) { $r = [IntPtr]::Zero; [void][GvrFontApi]::SendMessageTimeout([IntPtr]0xffff, 0x001D, [IntPtr]::Zero, [IntPtr]::Zero, 2, 1000, [ref]$r) }
    Log "  $added installed into Windows, $skipped already present"
}

function Install-Registry($root) {
    Log "--- game registry (private: $root\nascar_registry.ini) ---"

    # The game's HKLM\SOFTWARE\GlobalVR\NASCAR and HKLM\SOFTWARE\gvr\Plus keys are no longer
    # written: the GvrIO shim answers every registry call for them from nascar_registry.ini
    # (NASCAR_FINDINGS section 15). Folder values (launchFolder, GameRoot, PlusSchemaPath, ...)
    # are computed from the install folder at run time and the OEM values are built in, so a
    # fresh install only needs the display size here; NascarLaunch.exe rewrites the
    # nascar_settings.ini values on every start. Nothing machine-wide, no NFSU collision
    # (gvr\Plus\1.1\Cabinet is shared between the titles in the real registry).
    $store = Join-Path $root "nascar_registry.ini"
    if ($DryRun) { Log "  would write $store (display $Width x $Height)" }
    elseif (Test-Path $store) { Log "  $store already exists - kept (it holds the game's state)" }
    else {
        $text = "[GlobalVR\NASCAR]`r`nFullScreenWidth=`"$Width`"`r`nFullScreenHeight=`"$Height`"`r`n"
        [System.IO.File]::WriteAllText($store, $text, [System.Text.Encoding]::ASCII)
        Log "  created $store"
    }

    if (!$SkipShell) {
        # Anark Client 3.0 prefs (GvrShell.reg). HKCU, harmless, per-user.
        $anark = "HKCU:\Software\Anark\Client\3.0\Preferences"
        if ($DryRun) { Log "would write Anark client preferences" }
        else {
            if (!(Test-Path $anark)) { New-Item -Path $anark -Force | Out-Null }
            New-ItemProperty -Path $anark -Name "Initialized" -Value 1 -PropertyType DWord -Force | Out-Null
            New-ItemProperty -Path $anark -Name "InstallReturnCode" -Value 0 -PropertyType DWord -Force | Out-Null
            New-ItemProperty -Path $anark -Name "DisableGL" -Value "1" -PropertyType String -Force | Out-Null
        }
    }
}

# The XP-Embedded gate the OEM Setup.exe checks; read from the recovery image's
# own SYSTEM hive. Not needed by this installer or by the game.
function Install-OemRev {
    if (!$SetOemRev) { return }
    Log "--- XP Embedded OEM revision values ---"
    Warn "-SetOemRev only matters if you intend to run the ORIGINAL Setup.exe."
    $key = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
    foreach ($p in @(@("RUNTIMEOEMREV", "NASCAR,XP Embedded,HW Rev 945-G31,08252008"),
                     @("RUNTIMEOEMVERSION", "1.5.0.04"))) {
        if ($DryRun) { Log ("would set {0} = {1}" -f $p[0], $p[1]); continue }
        $cur = (Get-ItemProperty -Path $key -Name $p[0] -ErrorAction SilentlyContinue).($p[0])
        if ($cur -and $cur -ne $p[1] -and !$ForceOverwrite) { Warn "$($p[0]) already set to '$cur' - keeping it"; continue }
        New-ItemProperty -Path $key -Name $p[0] -Value $p[1] -PropertyType String -Force | Out-Null
        Log ("  {0} = {1}" -f $p[0], $p[1])
    }
}

# NascarLaunch.exe is what the player starts: it writes nascar_settings.ini into the
# game's private registry (nascar_registry.ini - no elevation) and then boots
# the cabinet - the Anark front end, attract mode, track/car selection, and the
# shell launches the race itself. Nothing is preset or forced.
function Install-Launcher($root, $game, $shell) {
    Log "--- launcher ---"
    $srcDir = Join-Path $SourceRoot "src\NascarLaunch"
    $exeSrc = Join-Path $srcDir "build\NascarLaunch.exe"
    $iniSrc = Join-Path $srcDir "nascar_settings.ini"
    $icoSrc = Join-Path $srcDir "NASCAR_GVR.ico"

    if (!(Test-Path $exeSrc)) {
        Warn "NascarLaunch.exe not built - run src\NascarLaunch\build.cmd (needs Visual Studio)."
        Warn "Falling back to a plain shell shortcut."
    }
    else { Copy-FileIfPresent $exeSrc (Join-Path $root "NascarLaunch.exe") "NascarLaunch.exe" }
    Copy-FileIfPresent $icoSrc (Join-Path $root "NASCAR_GVR.ico") "icon"

    # The settings file is the player's; never overwrite an edited one.
    $iniDst = Join-Path $root "nascar_settings.ini"
    if ((Test-Path $iniDst) -and !$ForceOverwrite) { Log "  keeping your existing nascar_settings.ini" }
    else {
        Copy-FileIfPresent $iniSrc $iniDst "nascar_settings.ini"
        # seed it with the values chosen on the command line
        if (!$DryRun -and (Test-Path $iniDst)) {
            $t = [IO.File]::ReadAllText($iniDst)
            $t = $t -replace '(?m)^Width=.*$',  "Width=$Width"
            $t = $t -replace '(?m)^Height=.*$', "Height=$Height"
            $t = $t -replace '(?m)^Fullscreen=.*$', ("Fullscreen=" + $(if ($Fullscreen) { "true" } else { "false" }))
            # Default to a borderless window that fills the screen (the race is windowed otherwise
            # and, at a size smaller than the desktop, shows as a small window). -Fullscreen opts
            # into exclusive fullscreen instead, so Borderless must be off for it to take effect.
            $t = $t -replace '(?m)^Borderless=.*$', ("Borderless=" + $(if ($Fullscreen) { "false" } else { "true" }))
            $t = $t -replace '(?m)^Track=.*$',  "Track=$Track"
            $t = $t -replace '(?m)^Series=.*$', "Series=$Series"
            if ($SkipShell) { $t = $t -replace '(?m)^Mode=shell\s*$', "Mode=race" }
            [IO.File]::WriteAllText($iniDst, $t)
            Log ("  nascar_settings.ini seeded (" + $(if ($Width -le 0) { "screen size" } else { "$Width x $Height" }) + ", " + $(if ($Fullscreen) { "fullscreen" } else { "borderless" }) + ")")
        }
    }

    if (!$NoShortcut -and !$DryRun) {
        $target = Join-Path $root "NascarLaunch.exe"
        $useLauncher = Test-Path $target
        $lnk = Join-Path ([Environment]::GetFolderPath("Desktop")) "NASCAR Team Racing.lnk"
        $ws = New-Object -ComObject WScript.Shell
        $s = $ws.CreateShortcut($lnk)
        if ($useLauncher) {
            $s.TargetPath = $target
            $s.IconLocation = "$target,0"
        }
        else {
            $s.TargetPath = Join-Path $shell "bin\AMPlayer.exe"
            $s.Arguments = "`"" + (Join-Path $shell "Shell.am") + "`" -v FULL"
            $s.IconLocation = (Join-Path $root "NASCAR_GVR.ico") + ",0"
        }
        $s.WorkingDirectory = $root
        $s.Description = "NASCAR Team Racing (GlobalVR)"
        $s.Save()
        Log "  desktop shortcut -> $($s.TargetPath)"

        # Start menu entry as well, so it behaves like a normal installed game.
        $sm = Join-Path ([Environment]::GetFolderPath("Programs")) "NASCAR Team Racing.lnk"
        $s2 = $ws.CreateShortcut($sm)
        $s2.TargetPath = $s.TargetPath
        $s2.Arguments = $s.Arguments
        $s2.WorkingDirectory = $root
        $s2.IconLocation = $s.IconLocation
        $s2.Description = $s.Description
        $s2.Save()
        Log "  start menu shortcut created"
    }
}

function Verify-Deployment($root, $game, $shell, $plus, $dbPath) {
    if ($DryRun) { Log "dry run: skipping verification"; return }
    Log "--- verification ---"
    $must = @(
        (Join-Path $game "NASCAR_GVR.exe"), (Join-Path $game "GvrIO.dll"),
        (Join-Path $game "msvcp71.dll"), (Join-Path $game "mss32.dll"),
        (Join-Path $game "config\GvrIO.xml"), (Join-Path $game "GameData"),
        (Join-Path $game "Audio"), (Join-Path $root "nascar_settings.ini")
    )
    if (!$SkipShell) { $must += (Join-Path $shell "bin\AMPlayer.exe"); $must += (Join-Path $shell "Shell.am") }
    if (!$SkipSqlite) {
        $must += $dbPath
        $must += (Join-Path $plus "4\schema\NASCARcabinetXml.enc")
        if (!$SkipShell) { $must += (Join-Path $shell "bin\PLUSDE.dll"); $must += (Join-Path $shell "bin\sqlite3.dll") }
    }
    $missing = @($must | Where-Object { !(Test-Path $_) })
    if ($missing.Count -gt 0) {
        foreach ($m in $missing) { Warn "MISSING: $m" }
        Fail "verification failed ($($missing.Count) missing path(s))"
    }
    $size = (Get-ChildItem -Recurse $root | Where-Object { !$_.PSIsContainer } | Measure-Object -Sum Length).Sum
    Log ("all required files present ({0:N0} MB installed)" -f ($size / 1MB))
}

function Invoke-Uninstall($root) {
    Log "=== UNINSTALL ==="
    if (Test-Path $root) {
        if (!$ForceOverwrite) { Warn "would remove $root  (re-run with -ForceOverwrite to delete)" }
        elseif ($DryRun) { Log "would remove $root" }
        else { Remove-Item -LiteralPath $root -Recurse -Force; Log "  removed $root" }
    }
    if ($ForceOverwrite) {
        Remove-Reg "GlobalVR\NASCAR"
        Remove-Reg "gvr\Plus\2.0\Cabinet"
        if (!$DryRun) {
            [Environment]::SetEnvironmentVariable("GVRSQLITE_DB_NAS1", $null, "Machine")
            Log "  cleared GVRSQLITE_DB_NAS1"
        }
    }
    else { Warn "would remove HKLM\SOFTWARE\GlobalVR\NASCAR + gvr\Plus\2.0\Cabinet and GVRSQLITE_DB_NAS1" }
    $lnk = Join-Path ([Environment]::GetFolderPath("Desktop")) "NASCAR Team Racing.lnk"
    if ((Test-Path $lnk) -and !$DryRun) { Remove-Item $lnk -Force; Log "  removed shortcut" }
    Log "uninstall pass complete"
}

# ============================ run =========================================

Log "============================================================"
Log "NASCAR GlobalVR portable installer $Version"
Log "============================================================"

# A dry run touches nothing, so it is allowed unelevated - that is the whole point
# of being able to preview an install before committing to it.
if (!(Test-Admin)) {
    if (!$DryRun) { Fail "Run this from an elevated Administrator PowerShell window (needed for the registry, fonts and the machine environment variable)." }
    Warn "not elevated - fine for -DryRun, but the real install needs an Administrator window"
}

if ([string]::IsNullOrEmpty($InstallRoot) -and (Test-CanShowDialogs)) {
    while ($true) {
        $pick = Select-Folder "Where should NASCAR Team Racing be installed?`r`nPick a folder or drive (for example C:\Games) - a NASCAR_GVR folder is created inside it." ""
        if (!$pick) { Log "cancelled - nothing was installed"; exit 0 }
        $pick = $pick.TrimEnd("\")
        if ($pick -match '^[A-Za-z]:$') { $InstallRoot = "$pick\NASCAR_GVR" }
        # an existing install folder (any NASCAR* name, or one holding the game) is used as it is
        elseif ((Split-Path -Leaf $pick) -match '(?i)^NASCAR' -or (Test-Path (Join-Path $pick "Game\NASCAR_GVR.exe"))) { $InstallRoot = $pick }
        else { $InstallRoot = Join-Path $pick "NASCAR_GVR" }
        if ($DryRun -or (Test-WritableFolder $InstallRoot)) { break }
        Show-Message "Cannot write to:`r`n$InstallRoot`r`n`r`nThat is probably the game disc or a read-only drive. Pick a folder on your hard drive, for example C:\Games." "Warning"
    }
}
if ([string]::IsNullOrEmpty($InstallRoot)) {
    Write-Host ""
    Write-Host "Where would you like to install NASCAR?" -ForegroundColor Cyan
    Write-Host "  Everything goes in this one folder - game, shell, GvrPlus and the database."
    Write-Host "  Nothing is written to C:\ root. About 800 MB is needed."
    Write-Host "  Examples:  C:\Games\NASCAR_GVR     D:\Games\NASCAR_GVR     E:\Arcade\NASCAR_GVR"
    Write-Host ""
    $def = "C:\Games\NASCAR_GVR"   # not D:\ - that is often the DVD drive holding the game disc
    $InstallRoot = Read-Host "Install folder [$def]"
    if ([string]::IsNullOrEmpty($InstallRoot)) { $InstallRoot = $def }
}
$InstallRoot = $InstallRoot.TrimEnd("\")
if ($InstallRoot -match '^[A-Za-z]:$' -or $InstallRoot -match '^[A-Za-z]:\\$') { Fail "Refusing to install to a drive root: $InstallRoot" }
if (!$DryRun -and !$Uninstall -and !(Test-WritableFolder $InstallRoot)) {
    Fail "Cannot write to $InstallRoot - it is probably the game disc or a read-only drive. Choose a folder on your hard drive, for example C:\Games\NASCAR_GVR."
}

$Game = Join-Path $InstallRoot "Game"
$Shell = Join-Path $InstallRoot "Shell"
$Gvr = Join-Path $InstallRoot "GVR"
$Plus = Join-Path $Gvr "GvrPlus"
$Db = Join-Path $Plus "game.db"

Log "install root : $InstallRoot"
Log "  game       : $Game"
Log "  shell      : $Shell"
Log "  plus + db  : $Plus"

if ($Uninstall) { Invoke-Uninstall $InstallRoot; exit 0 }

$already = Test-Path (Join-Path $Game "NASCAR_GVR.exe")
if ($already -and !$ForceOverwrite) {
    Log "game already present - refreshing registry/launchers/SQLite, skipping the big copy"
}
elseif ((Test-Path $Game) -and !$ForceOverwrite -and !$DryRun) {
    Fail "$Game exists but looks incomplete. Use -ForceOverwrite to redo, or pick another folder."
}

if (!(Test-Path (Join-Path $PayloadRoot "NASCAR\Game\NASCAR_GVR.exe")) -and !$ExtractIfMissing -and !$DryRun) {
    # nothing unpacked yet and no -DiscPath: find the mounted disc, else ask for it
    $cand = Find-NascarDisc
    if ($cand) { Log "game disc found: $cand" }
    elseif (Test-CanShowDialogs) {
        while ($true) {
            $pick = Select-Folder "Select the NASCAR Team Racing v1.1 game disc: the drive of the mounted ISO or disc (or a folder holding data1.cab)." "" -FromComputer
            if (!$pick) { Log "cancelled - nothing was installed"; exit 0 }
            $cand = $pick.TrimEnd("\"); if ($cand -match '^[A-Za-z]:$') { $cand += "\" }
            if (Test-Path (Join-Path $cand "data1.cab")) { break }
            Show-Message "data1.cab was not found in:`r`n$pick`r`n`r`nMount the NASCAR ISO (right-click it, Mount) and pick its drive." "Warning"
        }
    }
    else {
        while ($true) {
            Write-Host ""
            Write-Host "Insert or mount the NASCAR Team Racing game disc (the one with data1.cab)." -ForegroundColor Cyan
            $DiscPath = Read-Host "Disc drive or folder (e.g. E:\)"
            $cand = $DiscPath.Trim().Trim('"').TrimEnd("\")
            if ($cand -match '^[A-Za-z]:$') { $cand += "\" }
            if ($cand -and (Test-Path (Join-Path $cand "data1.cab"))) { break }
            Warn "data1.cab not found in '$DiscPath' - try again"
        }
    }
    $DiscRoot = $cand
    $ExtractRoot = Join-Path $env:TEMP "NASCAR_GVR_Extract"
    $PayloadRoot = Join-Path $ExtractRoot "File_Group"
    $ExtractIfMissing = $true
}
if (!(Test-Path (Join-Path $PayloadRoot "NASCAR\Game\NASCAR_GVR.exe"))) {
    if ($ExtractIfMissing) { Extract-Payload }
    else { Fail "Game payload not found at $PayloadRoot. Pass -ExtractIfMissing to unpack it from `"$DiscRoot`"." }
}

$drive = (Split-Path -Qualifier $InstallRoot) + "\"
$free = (Get-PSDrive ($drive[0]) -ErrorAction SilentlyContinue).Free
if ($free -and $free -lt 1GB) { Warn ("only {0:N1} GB free on {1} - about 0.8 GB is needed" -f ($free / 1GB), $drive) }

Ensure-DotNet
if (!$already -or $ForceOverwrite) { Install-Payload $Game $Shell $Gvr }
Fix-DirNames $Game
Install-SupportDlls $Game $Shell
Install-GvrIoShim $Game $Shell
Disable-CabinetPlugIns $Game
Repoint-ShellPaths $Shell
Apply-BytePatches $InstallRoot
Edit-OperatorXml $Shell
Edit-GvrIoXml $Game
Disable-GammaSet $Shell
Install-ClrPin $Shell
Install-Sqlite $Game $Shell $Plus $Db
Ensure-DirectX
Deploy-Dxvk $Game
Install-Fonts $InstallRoot
Install-Registry $InstallRoot
Install-OemRev
Install-Launcher $InstallRoot $Game $Shell
Verify-Deployment $InstallRoot $Game $Shell $Plus $Db

# the unpacked disc (~800 MB) is only needed during the install
if (!$DryRun -and $ExtractRoot -like (Join-Path $env:TEMP "*") -and (Test-Path $ExtractRoot)) {
    Remove-Item -LiteralPath $ExtractRoot -Recurse -Force -ErrorAction SilentlyContinue
    Log "removed the temporary disc extraction ($ExtractRoot)"
}

Log "============================================================"
Log "DONE - installed to $InstallRoot"
Log "  play   : $InstallRoot\NascarLaunch.exe  (or the desktop / Start-menu shortcut)"
Log "  config : $InstallRoot\nascar_settings.ini  - resolution, fullscreen, cabinet vs direct race"
if (!$SkipShell) { Log "  the launcher boots the cabinet front end: attract -> track/car select -> race" }
if (!$SkipSqlite) { Log "  db   : $Db (SQLite - no SQL Server anywhere)" }
Log "  log  : $LogFile"
Log ""
if (!$SkipSqlite) {
    Log "No log-off needed: NascarLaunch.exe points the shell and the race at this install's game.db."
}
Log "The HASP dongle check is satisfied by the GvrIO shim, so no cabinet hardware is needed."
Log "If anything goes wrong, read the game's own log first: $Game\LOG\trace00N.txt - it"
Log "records the command line, the dongle result, every startup state and each FATAL."
Log "============================================================"
if (!$DryRun) { Show-Message ("NASCAR Team Racing is installed in:" + [Environment]::NewLine + $InstallRoot + [Environment]::NewLine + [Environment]::NewLine + "Start it with the NASCAR Team Racing shortcut on your desktop." + [Environment]::NewLine + "Settings: nascar_settings.ini in that folder.") }
