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
    [switch]$RepairDotNet,   # remove .NET 1.1 and install it clean even if it seems to work
    [switch]$InstallDirectX,
    [switch]$Dxvk,
    [switch]$InstallHercules,
    [switch]$SetOemRev,
    [switch]$NoShortcut,
    [switch]$Fullscreen,
    [int]$Width = 0,     # 0 = use the primary screen size (borderless fills it, sharp)
    [int]$Height = 0,
    [string]$Track = "DAYTONA",
    [string]$Series = "2005NEXTEL",   # the only NEXTEL Cup season the v1.1 disc ships data for
    [switch]$Uninstall,
    [switch]$NoGui,
    [switch]$DryRun,
    [switch]$ForceOverwrite,
    [switch]$ResetData       # ALSO reset the player's data (leaderboards/stats db, settings, race saves) - backed up first
)

$ErrorActionPreference = "Stop"
$Version = "2026-10-12-nascar-portable-4.13" # 4.13: player data (leaderboards/stats db, settings, race saves) backed up before every install/update and kept by -ForceOverwrite (only -ResetData resets it); uninstall keeps a copy in Documents. 4.12: .NET 1.1 must pass a run test (else removed + reinstalled clean, -RepairDotNet forces it); no temp folder from an earlier run is reused (disc extraction, .NET images); unpacked disc checked complete. 4.11: install identity (gvr-install.ini) + update mode (no disc, settings merge); never installs into / updates / removes a folder holding anything else. 4.10: no CLR crash dialog can stall the .NET 1.1 install (regsvcs.exe on Win10). 4.9: release guard - stops before installing when any file of the release zip is missing (antivirus), no plain-shell fallback, launcher required; series default 2005NEXTEL. 4.8: launcher keeps the race above the shell's "Please Wait" overlay in borderless (rebuilt NascarLaunch.exe). 4.7: byte-patch stage logs each file + fails loudly on a non-v1.1 disc; verification logs every required file one by one

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
# ---- install identity: which folder is OUR install (updates never touch anything else) --------
# Every install carries gvr-install.ini (Product + Edition). The installer only updates a folder
# that is provably this product and edition; a folder holding anything else - another GVR title,
# another NASCAR game, the 2.0.2 "NASCAR Team Racing" edition, or just other files - is never
# installed into, updated or removed. Installs from before the marker existed (<= 1.0.8) are
# recognised by the exact v1.1 game exe (we never modify it on disk) PLUS our launcher or settings.
$Product = "NASCAR_GVR"
$Edition = "1.1"
$MarkerName = "gvr-install.ini"
$Exe11Sha256  = "4F61C7D63BD3C70AAB5FAEDFEE55C5587336E83F67ABC9A4C4637B2F66DDE482"   # v1.1 disc 050-0136-01
$Exe202Sha256 = "316FD6E422CBDF746E41E73BD4D229342389CA8F1C9E5154D10916759E777355"   # 2.0.2.068 (Team Racing)

function Get-FileSha256($p) {   # PowerShell 2.0 has no Get-FileHash
    $fs = [IO.File]::OpenRead($p)
    try { return ([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fs))).Replace("-", "") }
    finally { $fs.Close() }
}

function Read-InstallMarker($root) {
    $m = @{}
    $p = Join-Path $root $MarkerName
    if (!(Test-Path -LiteralPath $p)) { return $null }
    foreach ($l in (Get-Content -LiteralPath $p)) {
        if ($l -match '^\s*([A-Za-z]+)\s*=\s*(.*?)\s*$') { $m[$matches[1]] = $matches[2] }
    }
    return $m
}

# Returns one of:
#   empty         the folder does not exist or is empty           -> fresh install
#   ours          our marker, this product + edition              -> update in place (no disc)
#   ours-legacy   no marker, but the v1.1 exe + our launcher/ini  -> update in place, marker added
#   ours-partial  only our own files, no game yet (a stopped install) -> fresh install
#   other:<what>  another product / edition / a foreign NASCAR_GVR.exe -> never touched
#   foreign       anything else that is not empty                 -> never touched
function Get-InstallKind($root) {
    if (!(Test-Path -LiteralPath $root)) { return "empty" }
    if (!(Test-Path -LiteralPath $root -PathType Container)) { return "foreign" }
    $items = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue)
    if ($items.Count -eq 0) { return "empty" }
    $m = Read-InstallMarker $root
    if ($m) {
        if ($m["Product"] -eq $Product -and $m["Edition"] -eq $Edition) { return "ours" }
        return ("other:" + $m["Product"] + " " + $m["Edition"])
    }
    $ourFile = (Test-Path -LiteralPath (Join-Path $root "NascarLaunch.exe")) -or
               (Test-Path -LiteralPath (Join-Path $root "nascar_settings.ini"))
    $exe = Join-Path $root "Game\NASCAR_GVR.exe"
    if (Test-Path -LiteralPath $exe) {
        $h = Get-FileSha256 $exe
        if ($h -eq $Exe11Sha256 -and $ourFile) { return "ours-legacy" }
        if ($h -eq $Exe202Sha256) { return "other:NASCAR Team Racing 2.0.2" }
        if ($h -eq $Exe11Sha256) { return "other:a NASCAR GVR 1.1 copy that this installer did not make" }
        return "other:a different NASCAR_GVR.exe"
    }
    # a stopped earlier install: nothing but our own top-level names
    $ours = @("Game", "Shell", "GVR", "LOG", "Fonts", "NascarLaunch.exe", "nascar_settings.ini",
              "nascar_registry.ini", "NASCAR_GVR.ico", $MarkerName)
    $strangers = @($items | Where-Object { $ours -notcontains $_.Name })
    if ($ourFile -and $strangers.Count -eq 0) { return "ours-partial" }
    return "foreign"
}

function Test-OurKind($kind) { return @("empty", "ours", "ours-legacy", "ours-partial") -contains $kind }

function Get-KindText($kind) {
    if ($kind -like "other:*") { return $kind.Substring(6) }
    return "other files (not a NASCAR GVR 1.1 install)"
}

function Write-InstallMarker($root) {
    if ($DryRun) { Log "would write $MarkerName (Product=$Product, Edition=$Edition)"; return }
    $old = Read-InstallMarker $root
    $first = if ($old -and $old["FirstInstalled"]) { $old["FirstInstalled"] } else { (Get-Date).ToString("yyyy-MM-dd HH:mm") }
    $nl = [Environment]::NewLine
    $text = "; NASCAR GVR install identity - lets the installer update THIS folder (and only this one)." + $nl +
            "; Written by Install-NASCAR-GVR-Portable.ps1. Do not edit or copy it into another folder." + $nl +
            "[Install]" + $nl +
            "Product=$Product" + $nl + "Edition=$Edition" + $nl +
            "Installer=$Version" + $nl +
            "FirstInstalled=$first" + $nl +
            "Updated=" + (Get-Date).ToString("yyyy-MM-dd HH:mm") + $nl
    [IO.File]::WriteAllText((Join-Path $root $MarkerName), $text)
    Log "install identity written: $MarkerName (Product=$Product, Edition=$Edition, Installer=$Version)"
}

# A shortcut we may replace or delete: missing, or pointing into this install (or at nothing).
function Test-OurShortcut($lnk, $root) {
    if (!(Test-Path -LiteralPath $lnk)) { return $true }
    try {
        $t = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk).TargetPath
        if (!$t -or !(Test-Path -LiteralPath $t)) { return $true }
        return $t.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)
    } catch { return $false }
}

# Settings merge on update: add every section / setting the new template has and the player's
# nascar_settings.ini lacks (with the comment lines just above it), and never change a value the
# player has. Unknown keys of theirs are left alone. PowerShell 2.0 compatible.
function Merge-Settings($dst, $tpl) {
    if (!(Test-Path -LiteralPath $tpl) -or !(Test-Path -LiteralPath $dst)) { return }
    $user = New-Object System.Collections.ArrayList
    foreach ($l in [IO.File]::ReadAllLines($dst)) { [void]$user.Add($l) }
    $secs = @{}; $cur = ""; $secs[""] = @{ keys = @{}; last = -1 }
    $have = @{}                                       # every line the player's file has (trimmed)
    foreach ($l in $user) { $have[([string]$l).Trim()] = 1 }
    for ($i = 0; $i -lt $user.Count; $i++) {
        $l = ([string]$user[$i]).Trim()
        if ($l -match '^\[(.+)\]$') { $cur = $matches[1].Trim().ToLower(); $secs[$cur] = @{ keys = @{}; last = $i }; continue }
        if ($l -and !$l.StartsWith(";") -and !$l.StartsWith("#") -and $l.Contains("=")) {
            $secs[$cur].keys[$l.Split("=")[0].Trim().ToLower()] = 1
        }
        if ($l) { $secs[$cur].last = $i }
    }
    $atIndex = @{}                                    # user line index -> lines to insert after it
    $append = New-Object System.Collections.ArrayList # whole new sections
    $added = 0; $tsec = ""; $newSec = $false; $pending = @()
    foreach ($raw in [IO.File]::ReadAllLines($tpl)) {
        $l = $raw.Trim()
        if ($l -match '^\[(.+)\]$') {
            $tsec = $matches[1].Trim().ToLower(); $pending = @()
            $newSec = !$secs.ContainsKey($tsec)
            if ($newSec) { [void]$append.Add(""); [void]$append.Add($raw) }
            continue
        }
        if ($newSec) { [void]$append.Add($raw); if ($l -and !$l.StartsWith(";") -and $l.Contains("=")) { $added++ }; continue }
        if (!$l) { $pending = @(); continue }
        if ($l.StartsWith(";") -or $l.StartsWith("#")) { $pending += $raw; continue }
        if (!$l.Contains("=") -or !$secs.ContainsKey($tsec)) { $pending = @(); continue }
        $k = $l.Split("=")[0].Trim().ToLower()
        if (!$secs[$tsec].keys.ContainsKey($k)) {
            $at = $secs[$tsec].last
            if (!$atIndex.ContainsKey($at)) { $atIndex[$at] = New-Object System.Collections.ArrayList }
            # the comment lines just above the new key - unless the player's file has them already
            foreach ($p in $pending) { if (!$have.ContainsKey($p.Trim())) { [void]$atIndex[$at].Add($p); $have[$p.Trim()] = 1 } }
            [void]$atIndex[$at].Add($raw)
            $secs[$tsec].keys[$k] = 1
            $added++
        }
        $pending = @()
    }
    if ($added -eq 0) { Log "  settings: nothing new to add"; return }
    if ($DryRun) { Log "  would add $added new setting(s) to nascar_settings.ini (your values unchanged)"; return }
    foreach ($at in ($atIndex.Keys | Sort-Object -Descending)) { $user.InsertRange($at + 1, $atIndex[$at]) }
    if ($append.Count) { $user.AddRange($append) }
    [IO.File]::WriteAllLines($dst, [string[]]$user.ToArray())
    Log "  settings: added $added new setting(s) to nascar_settings.ini (your values unchanged)"
}

# ---- the player's data: never lost by an install, update or repair ---------------------------
# Leaderboards, player/driver/track stats, players and the operator's settings live in
# GVR\GvrPlus\game.db; the player's settings in nascar_settings.ini; the game's own state in
# nascar_registry.ini; the race's profile/controls/garage in Game\Save. An update never replaces
# them; a -ForceOverwrite reinstall keeps them too (only -ResetData resets them). Before every
# install/update they are copied to <install>\Backup\<time> (newest 5 kept), and uninstall copies
# them to Documents\NASCAR_GVR Backup before the folder is removed.
$PlayerDataItems = @("GVR\GvrPlus\game.db", "nascar_settings.ini", "nascar_registry.ini", "Game\Save")
function Backup-PlayerData($root, $dest) {
    $have = @($PlayerDataItems | Where-Object { Test-Path -LiteralPath (Join-Path $root $_) })
    if ($have.Count -eq 0) { return $null }
    if ($DryRun) { Log "would back up the player's data ($($have -join ', ')) to $dest"; return $null }
    foreach ($i in $have) {
        $to = Join-Path $dest $i
        New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
        Copy-Item -LiteralPath (Join-Path $root $i) -Destination $to -Recurse -Force
    }
    Log "player data backed up ($($have -join ', ')) -> $dest"
    return $dest
}
function Remove-OldBackups($root) {
    $dir = Join-Path $root "Backup"
    $old = @(Get-ChildItem -LiteralPath $dir -ErrorAction SilentlyContinue | Where-Object { $_.PSIsContainer } |
             Sort-Object Name -Descending | Select-Object -Skip 5)
    foreach ($o in $old) { Remove-Item -LiteralPath $o.FullName -Recurse -Force -ErrorAction SilentlyContinue }
}
function Restore-RaceSaves($backup, $game) {
    $src = Join-Path $backup "Game\Save"
    if (!$backup -or !(Test-Path -LiteralPath $src)) { return }
    # file by file: Copy-Item of a folder INTO an existing folder of the same name nests it
    # (Save\GvrSinglePlayer\GvrSinglePlayer\...) instead of replacing the files
    $dst = Join-Path $game "Save"; $n = 0
    foreach ($f in @(Get-ChildItem -LiteralPath $src -Recurse -Force | Where-Object { !$_.PSIsContainer })) {
        $to = Join-Path $dst $f.FullName.Substring($src.Length).TrimStart('\')
        New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $to -Force
        $n++
    }
    Log "  the player's race saves restored over the disc defaults ($n file(s) in Game\Save)"
}

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

# The parts of the unpacked disc the install copies. An extraction can be left half-done in %TEMP%
# by an interrupted install; checking only for NASCAR_GVR.exe let the next run reuse it and miss
# the database files (2026-10-12: "GvrPlus payload source missing" -> NASCARcabinetXml.enc missing).
$PayloadParts = @("NASCAR\Game\NASCAR_GVR.exe", "NASCAR\Shell\bin\AMPlayer.exe",
                  "GVRPLUS\4\schema\NASCARcabinetXml.enc", "WINDOWS\system32")
function Get-MissingPayloadParts {
    $m = @()
    foreach ($part in $PayloadParts) { if (!(Test-Path -LiteralPath (Join-Path $PayloadRoot $part))) { $m += $part } }
    return $m
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
    $miss = @(Get-MissingPayloadParts)
    if ($miss.Count) { Fail ("The game disc was not unpacked completely - missing: " + ($miss -join ", ") + ". Make sure the disc/ISO is the v1.1 game disc and readable, then run Install.bat again.") }
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
        # Assert-ReleaseFiles already stops a release install without it; a dev checkout must build it.
        Fail "GvrIO.dll (the shim) is missing: $shim - without it the game hangs on startup. Unzip the release again (antivirus?) or run src\GvrIOShim\build.cmd."
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
    # They come from the game disc and are already in place from the first install.
    if ($UpdateMode) { Log "--- support DLLs: already in place (update - no disc) ---"; return }
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

    if (!$UpdateMode) {   # from the disc; in place already on update
        if (!$DryRun -and !(Test-Path -LiteralPath (Join-Path $PayloadRoot "GVRPLUS"))) { Fail "The game disc's GVRPLUS folder (database files) was not unpacked - run Install.bat again with the v1.1 disc mounted." }
        Copy-Contents (Join-Path $PayloadRoot "GVRPLUS") $plus "GvrPlus payload"
    }

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
    if ((Test-Path $dbPath) -and !$ResetData) {   # leaderboards, stats, operator settings - never reset by a reinstall
        Log "  existing database kept (leaderboards, stats, operator settings): $dbPath  (-ResetData resets it)"
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
    $unpatched = New-Object System.Collections.ArrayList   # present but matched neither OEM nor our patch -> a different build
    $missing   = New-Object System.Collections.ArrayList   # not found in the payload at all
    # Files the game cannot start without. If one of these will not patch, the disc is not the
    # supported build, so we stop loudly (below) instead of finishing an install that looks fine
    # and then hangs at launch with no window.
    $critical = @("Shell\bin\Dongle.dll")
    # Format a slice of a byte array as lowercase hex, for "expected vs found" diagnostics.
    function HexOf($data, $off, $len) {
        if ($off -ge $data.Length) { return "<past end of file>" }
        $end = [Math]::Min($off + $len, $data.Length)
        $sb = New-Object System.Text.StringBuilder
        for ($i = $off; $i -lt $end; $i++) { [void]$sb.AppendFormat("{0:x2}", $data[$i]) }
        return $sb.ToString()
    }
    foreach ($rel in $files.Keys) {
        $path = Join-Path $root $rel
        if (!(Test-Path -LiteralPath $path)) {
            if ($DryRun) { Log "  $rel : not present (would skip)" } else { Warn "  $rel : not found - not patched" }
            [void]$missing.Add($rel); continue
        }
        $data = [IO.File]::ReadAllBytes($path)
        $isOem = $true; $isNew = $true
        foreach ($p in $files[$rel]) {
            if (!(Matches $data $p[0] (HexBytes $p[1]))) { $isOem = $false }
            if (!(Matches $data $p[0] (HexBytes $p[2]))) { $isNew = $false }
        }
        if ($isNew) { Log ("  {0} : already patched ({1} change(s), {2} bytes)" -f $rel, $files[$rel].Count, $data.Length); $already++; continue }
        if (!$isOem -and (Test-Path -LiteralPath "$path.oem")) {
            # an older release's patch: start again from the OEM copy kept beside it
            $orig = [IO.File]::ReadAllBytes("$path.oem"); $origOk = $true
            foreach ($p in $files[$rel]) { if (!(Matches $orig $p[0] (HexBytes $p[1]))) { $origOk = $false } }
            if ($origOk) { $data = $orig; $isOem = $true; Log "  $rel : updating an older patch from $rel.oem" }
        }
        if (!$isOem) {
            # Matches neither the OEM bytes nor our patched bytes: a different build of this file.
            # Log the first few offsets with expected-vs-found bytes so a support log pins the disc.
            Warn ("  {0} : does not match the supported v1.1 build - left unpatched ({1} bytes)" -f $rel, $data.Length)
            $shown = 0
            foreach ($p in $files[$rel]) {
                if (!(Matches $data $p[0] (HexBytes $p[1]))) {
                    Warn ("      offset 0x{0:x}: expected OEM {1} / patched {2}, found {3}" -f $p[0], $p[1], $p[2], (HexOf $data $p[0] ($p[1].Length / 2)))
                    $shown++; if ($shown -ge 3) { Warn "      (further mismatches not shown)"; break }
                }
            }
            [void]$unpatched.Add($rel); continue
        }
        if ($DryRun) { Log "  $rel : OEM v1.1 ok - would patch ($($files[$rel].Count) change(s))"; continue }
        $bak = "$path.oem"
        if (!(Test-Path -LiteralPath $bak)) { [IO.File]::WriteAllBytes($bak, $data) }
        foreach ($p in $files[$rel]) { $b = HexBytes $p[2]; [Array]::Copy($b, 0, $data, $p[0], $b.Length) }
        [IO.File]::WriteAllBytes($path, $data)
        Log ("  {0} : patched ({1} change(s), {2} bytes; OEM kept as *.oem)" -f $rel, $files[$rel].Count, $data.Length)
        $done++
    }
    Log ("  summary: {0} patched, {1} already patched, {2} unpatched, {3} missing (OEM copies kept as *.oem)" -f $done, $already, $unpatched.Count, $missing.Count)
    if ($unpatched.Count -gt 0) { Log ("  unpatched (different build): " + ($unpatched -join ", ")) }
    if ($missing.Count   -gt 0) { Log ("  missing from payload: "        + ($missing   -join ", ")) }

    # Loud stop if a file the game cannot start without could not be patched. This is almost
    # always the wrong disc: the fixes are cut from the NASCAR v1.1 game disc (part 050-0136-01)
    # and silently skip on any other revision (e.g. v1.5), leaving an install that looks fine but
    # hangs at launch with no window. Fail here, with the reason, rather than there with none.
    $criticalBad = @()
    foreach ($c in $critical) { if (($unpatched -contains $c) -or ($missing -contains $c)) { $criticalBad += $c } }
    if ($criticalBad.Count -gt 0) {
        Warn "=============================================================="
        Warn "This NASCAR game disc is NOT the supported version."
        Warn "Required file(s) did not match the expected v1.1 bytes:"
        foreach ($c in $criticalBad) { Warn "    $c" }
        Warn "The fixes are made for the v1.1 game disc (part 050-0136-01, the"
        Warn "one with data1.cab). Other versions - notably the v1.5 disc -"
        Warn "install but will not start: the shell hangs with no window."
        Warn "Get the v1.1 disc/ISO and reinstall. The README has a download link."
        Warn "=============================================================="
        Fail ("unsupported game disc: " + ($criticalBad -join ", ") + " did not match the supported v1.1 build (part 050-0136-01). Only v1.1 is compatible - see the README.")
    }
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
# ---- .NET 1.1 crash dialogs must not stall the silent install ----------------------------------
# During the .NET 1.1 MSI install, regsvcs.exe (COM+ registration for System.EnterpriseServices,
# which the games do not use) can crash - seen on a fresh Windows 10 VM (2026-10-11). CLR 1.1
# then shows its "Application has generated an exception that could not be handled" JIT-debug
# dialog and waits for a click, so the silent install sits at "installing .NET 1.1". With
# DbgJITDebugLaunchSetting = 1, CLR 1.1 ends a crashed process without asking. Set for the MSI
# install only, in both registry views (CLR 1.1 is 32-bit: WOW6432Node on 64-bit Windows), and
# put back afterwards - removed again where it did not exist. Returns whatever $Body outputs.
function Invoke-QuietClrCrashes([scriptblock]$Body) {
    $keys = @("HKLM:\SOFTWARE\Microsoft\.NETFramework")
    if (Test-Path "HKLM:\SOFTWARE\WOW6432Node") { $keys += "HKLM:\SOFTWARE\WOW6432Node\Microsoft\.NETFramework" }
    $saved = @()
    foreach ($k in $keys) {
        $old = $null
        try { $old = (Get-ItemProperty -Path $k -Name DbgJITDebugLaunchSetting -ErrorAction Stop).DbgJITDebugLaunchSetting } catch {}
        $saved += ,@($k, $old)
        try {
            if (!(Test-Path $k)) { New-Item -Path $k -Force | Out-Null }
            New-ItemProperty -Path $k -Name DbgJITDebugLaunchSetting -PropertyType DWord -Value 1 -Force | Out-Null
        } catch { Warn "could not set DbgJITDebugLaunchSetting in $k ($($_.Exception.Message))" }
    }
    Log "  CLR crash dialogs off for the .NET install (DbgJITDebugLaunchSetting = 1)"
    try { & $Body }
    finally {
        foreach ($s in $saved) {
            try {
                if ($null -eq $s[1]) { Remove-ItemProperty -Path $s[0] -Name DbgJITDebugLaunchSetting -ErrorAction SilentlyContinue }
                else { New-ItemProperty -Path $s[0] -Name DbgJITDebugLaunchSetting -PropertyType DWord -Value $s[1] -Force | Out-Null }
            } catch {}
        }
        Log "  CLR crash dialog setting restored"
    }
}

# ---- .NET Framework 1.1: present AND working, or reinstalled clean ----------------------------
# A damaged 1.1 install makes the shell hang inside CLR startup with no window (2026-10-12: a
# player's 1.1, left half-done by the regsvcs.exe crash dialog, was fixed by uninstall + clean
# reinstall). So an existing 1.1 is not trusted by its files alone: a tiny 1.1 program doing what
# the shell's .NET does first (culture + number/currency/date formatting, System.Data) must run and
# exit within 30 s. If not, 1.1 is removed and installed clean. Everything this stage unpacks goes
# into FRESH folders - nothing left in %TEMP% by an earlier, interrupted run is ever reused.
$NetFx11ProductCode = "{CB2F7EDD-9D1F-43C1-90FC-4F52EAE172A1}"   # netfx.msi, every language edition
$NetFx11Dir = Join-Path $env:WINDIR "Microsoft.NET\Framework\v1.1.4322"

function Reset-Dir($p) {
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $p) { Fail "Could not clear the temporary folder $p - close any window using it and run Install.bat again." }
    New-Item -ItemType Directory -Force $p | Out-Null
}

# $null when .NET 1.1 works, else the reason.
function Test-DotNet11Works {
    foreach ($f in "mscorlib.dll", "mscorwks.dll", "System.dll", "System.Data.dll", "csc.exe") {
        if (!(Test-Path -LiteralPath (Join-Path $NetFx11Dir $f))) { return "$f is missing" }
    }
    if (!(Test-Path (Join-Path $env:WINDIR "assembly\GAC\System\1.0.5000.0__b77a5c561934e089\System.dll"))) {
        return "the 1.1 System assembly is missing from the global assembly cache"
    }
    $t = Join-Path $LogDir "dotnet11_test"
    Reset-Dir $t
    $src = @'
using System; using System.Globalization;
class P {
    static int Main() {
        CultureInfo c = CultureInfo.CurrentCulture;
        string s = (1234567.891).ToString("N", c) + " | " + (12.5m).ToString("C", c) + " | " + DateTime.Now.ToString(c);
        System.Data.DataSet d = new System.Data.DataSet("t"); d.Tables.Add("x");
        Console.WriteLine(Environment.Version + " " + c.Name + " " + s);
        return 42;
    }
}
'@
    [IO.File]::WriteAllText((Join-Path $t "P.cs"), $src)
    [IO.File]::WriteAllText((Join-Path $t "P.exe.config"),
        '<?xml version="1.0"?><configuration><startup><requiredRuntime version="v1.1.4322" safemode="true"/><supportedRuntime version="v1.1.4322"/></startup></configuration>')
    $csc = Join-Path $NetFx11Dir "csc.exe"
    $p = Start-Process $csc -ArgumentList "/nologo /out:`"$t\P.exe`" /r:System.Data.dll `"$t\P.cs`"" -WorkingDirectory $t -WindowStyle Hidden -Wait -PassThru `
                        -RedirectStandardOutput (Join-Path $t "csc.txt")
    if ($p.ExitCode -ne 0 -or !(Test-Path (Join-Path $t "P.exe"))) { return "its C# compiler failed (exit $($p.ExitCode))" }
    $out = Join-Path $t "out.txt"
    $why = Invoke-QuietClrCrashes {
        $q = Start-Process (Join-Path $t "P.exe") -WorkingDirectory $t -WindowStyle Hidden -PassThru -RedirectStandardOutput $out
        $null = $q.Handle   # keep the handle: without it ExitCode is empty after WaitForExit(timeout)
        if (!$q.WaitForExit(30000)) {
            try { $q.Kill() } catch { }
            "a .NET 1.1 test program hung (no exit within 30 s) - the same hang the shell shows"
        }
        elseif ($q.ExitCode -ne 42) { "a .NET 1.1 test program crashed (exit $($q.ExitCode))" }
    }
    if ($why) { return $why }
    $line = if (Test-Path $out) { (Get-Content $out | Select-Object -First 1) } else { "" }
    if ($line -notlike "1.1.4322*") { return "the test program did not run on .NET 1.1 (it reported '$line')" }
    Log "  .NET 1.1 test: $line"
    return $null
}

function Remove-DotNet11 {
    Log "  removing the existing .NET 1.1 ($NetFx11ProductCode)"
    $code = Invoke-QuietClrCrashes { (Start-Process msiexec -ArgumentList "/x $NetFx11ProductCode /qn /norestart" -Wait -PassThru).ExitCode }
    if ($code -eq 1605) { Log "  (it was not registered with Windows Installer - installing over it)" }
    elseif ($code -ne 0 -and $code -ne 3010) { Warn "  .NET 1.1 removal returned $code - installing over it anyway" }
    else { Log "  removed" }
}

function Install-DotNet11($dfx, $sp1) {
    Log "installing .NET 1.1 + SP1 (slipstreamed - required on Windows 10/11)"
    $w = Join-Path $LogDir "dotnet11"; $adm = Join-Path $LogDir "dotnet11_admin"
    Reset-Dir $w; Reset-Dir $adm                      # never an image left by an earlier run
    $p = Start-Process $dfx -ArgumentList "/q /c /t:`"$w`"" -Wait -PassThru
    if (!(Test-Path (Join-Path $w "netfx.msi"))) { Fail "dotnetfx.exe extraction failed ($($p.ExitCode))" }
    $p = Start-Process $sp1 -ArgumentList "/Xp:`"$w\netfxsp.msp`"" -Wait -PassThru
    if (!(Test-Path (Join-Path $w "netfxsp.msp"))) { Fail ".NET 1.1 SP1 extraction failed ($($p.ExitCode))" }
    $p = Start-Process msiexec -ArgumentList "/a `"$w\netfx.msi`" TARGETDIR=`"$adm`" /qn" -Wait -PassThru
    if (!(Test-Path (Join-Path $adm "netfx.msi"))) { Fail ".NET 1.1 admin image creation failed ($($p.ExitCode))" }
    $p = Start-Process msiexec -ArgumentList "/a `"$adm\netfx.msi`" /p `"$w\netfxsp.msp`" /qn" -Wait -PassThru
    if ($p.ExitCode -ne 0) { Fail ".NET 1.1 SP1 slipstream failed ($($p.ExitCode))" }
    $code = Invoke-QuietClrCrashes { (Start-Process msiexec -ArgumentList "/i `"$adm\netfx.msi`" /qn /norestart" -Wait -PassThru).ExitCode }
    if ($code -ne 0 -and $code -ne 3010) { Fail ".NET 1.1 SP1 install failed ($code)" }
    Remove-Item -LiteralPath $w, $adm -Recurse -Force -ErrorAction SilentlyContinue
    Log "  .NET 1.1 SP1 installed"
}

function Ensure-DotNet {
    if ($SkipDotNet -or $SkipShell) { return }
    $present = Test-Path -LiteralPath (Join-Path $NetFx11Dir "mscorlib.dll")
    if ($present -and !$RepairDotNet) {
        if ($DryRun) { Log ".NET 1.1 present (dry run: not tested)"; return }
        $why = Test-DotNet11Works
        if (!$why) { Log ".NET 1.1 present and working"; return }
        Warn ".NET 1.1 is installed but does not work ($why) - reinstalling it clean"
    }
    elseif ($present) { Log ".NET 1.1: clean reinstall requested (-RepairDotNet)" }

    $dep = Resolve-Dependencies
    $dfx = if ($dep) { Join-Path $dep "DotNet11\dotnetfx.exe" } else { $null }
    $sp1 = if ($dep) { Join-Path $dep "DotNet11\NDP1.1sp1-KB867460-X86.exe" } else { $null }
    if (!$dfx -or !(Test-Path $dfx)) {
        if ($present) { Fail ".NET 1.1 does not work and its installer (Dependencies\DotNet11\dotnetfx.exe) is not in this folder - unzip the release again." }
        Warn ".NET 1.1 is missing and the redist is not bundled. The game itself does not need it,"
        Warn "but the Anark shell + PLUSDE (the database layer) do. Install it, or use -SkipShell."
        return
    }
    if (!(Test-Path $sp1)) { Fail "NDP1.1sp1-KB867460-X86.exe is required on Windows 10/11 (plain redist = MSI 1603)" }
    if ($DryRun) { Log $(if ($present) { "would remove and reinstall .NET 1.1 SP1" } else { "would install .NET 1.1 SP1 (slipstreamed)" }); return }

    if ($present) { Remove-DotNet11 }
    Install-DotNet11 $dfx $sp1
    $why = Test-DotNet11Works
    if ($why) {
        Fail (".NET 1.1 still does not work after a clean reinstall ($why). If an antivirus is active, add the install folder " +
              "and %TEMP%\NASCAR_GVR_Install as exclusions and run Install.bat again; otherwise send us %TEMP%\NASCAR_GVR_Install\install.log.")
    }
    Log "  .NET 1.1 works"
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
        # It IS in every release zip (and needs no Visual Studio to run); Assert-ReleaseFiles stops a
        # release install without it. No plain-shell fallback any more: without the launcher the game
        # runs without its settings or private-registry preload and looks broken. A developer
        # checkout must build it (src\NascarLaunch\build.cmd).
        Fail "NascarLaunch.exe is missing: $exeSrc - unzip the release again (antivirus?) and re-run Install.bat."
    }
    Copy-FileIfPresent $exeSrc (Join-Path $root "NascarLaunch.exe") "NascarLaunch.exe"
    Copy-FileIfPresent $icoSrc (Join-Path $root "NASCAR_GVR.ico") "icon"

    # The settings file is the player's; never overwrite an edited one.
    $iniDst = Join-Path $root "nascar_settings.ini"
    if ((Test-Path $iniDst) -and !$ResetData) { Log "  keeping your existing nascar_settings.ini"; Merge-Settings $iniDst $iniSrc }
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
        # never take over a shortcut that starts ANOTHER install (another copy, a future edition)
        $lnkName = "NASCAR Team Racing"
        $lnk = Join-Path ([Environment]::GetFolderPath("Desktop")) "$lnkName.lnk"
        if (!(Test-OurShortcut $lnk $root)) {
            $lnkName = "NASCAR Team Racing (" + (Split-Path -Leaf $root) + ")"
            $lnk = Join-Path ([Environment]::GetFolderPath("Desktop")) "$lnkName.lnk"
            Log "  a 'NASCAR Team Racing' shortcut already starts another install - this one is '$lnkName'"
        }
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
        $sm = Join-Path ([Environment]::GetFolderPath("Programs")) "$lnkName.lnk"
        if (!(Test-OurShortcut $sm $root)) { Log "  start menu: '$lnkName' starts another install - left alone"; return }
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
    Log "--- verification (each required file, one by one) ---"

    # Each check is @(path, required, note). required $true -> the install fails if it is absent;
    # required $false -> a warning only (the game may still start, but note it in the log). Every
    # file is logged either way, so a "won't start" support log shows exactly what is and isn't there.
    $checks = New-Object System.Collections.ArrayList
    function Need($p, $req, $note) { [void]$checks.Add(@($p, $req, $note)) }

    Need (Join-Path $game "NASCAR_GVR.exe")        $true  "race engine"
    Need (Join-Path $game "GvrIO.dll")             $true  "GvrIO shim (dongle gate + mode limit, loaded into the race)"
    Need (Join-Path $game "msvcp71.dll")           $true  "MSVC 7.1 runtime"
    Need (Join-Path $game "mss32.dll")             $true  "Miles audio"
    Need (Join-Path $game "config\GvrIO.xml")      $true  "input map"
    Need (Join-Path $game "GameData")              $true  "track / car data"
    Need (Join-Path $game "Audio")                 $true  "audio banks"
    Need (Join-Path $game "LOG")                   $false "crash/trace log folder (engine will not create it; no folder = no trace00N.txt)"
    Need (Join-Path $root "nascar_settings.ini")   $true  "display / controller settings"
    Need (Join-Path $root "nascar_registry.ini")   $false "private registry the shim reads"
    Need (Join-Path $root "NascarLaunch.exe")      $true  "launcher the shortcut runs"
    Need (Join-Path $game "GvrIO_oem.dll")         $true  "OEM GvrIO the shim forwards to"

    if (!$SkipShell) {
        Need (Join-Path $shell "bin\AMPlayer.exe")            $true  "Anark shell host"
        Need (Join-Path $shell "Shell.am")                   $true  "shell start scene"
        Need (Join-Path $shell "bin\GvrIO.dll")              $true  "GvrIO shim for the shell (private registry, startup deadlock)"
        Need (Join-Path $shell "bin\GvrIO_oem.dll")          $true  "OEM GvrIO the shell's shim forwards to"
        Need (Join-Path $shell "bin\Dongle.dll")             $true  "shell dongle self-test (must be the v1.1-patched build)"
        Need (Join-Path $shell "bin\AMPlayer.exe.config")    $false "pins CLR 1.1 (PLUSDE access-violates on CLR 2.0 without it)"
        Need (Join-Path $shell "bin\msvcp71d.dll")           $false "debug runtime (NASCARPlugIn silently fails to load without it)"
        Need (Join-Path $shell "bin\msvcr71d.dll")           $false "debug runtime (NASCARPlugIn silently fails to load without it)"
        Need (Join-Path $shell "bin\DongleStorageDevice.dll") $false "OEM storage device (car select needs the real one)"
    }
    if (!$SkipSqlite) {
        Need $dbPath                                           $true "SQLite game database"
        Need (Join-Path $plus "4\schema\NASCARcabinetXml.enc") $true "Plus connection blob"
        if (!$SkipShell) {
            Need (Join-Path $shell "bin\PLUSDE.dll")  $true "Plus provider (patched to SQLite)"
            Need (Join-Path $shell "bin\sqlite3.dll") $true "SQLite engine"
        }
    }

    $missing = 0; $absent = 0
    foreach ($c in $checks) {
        $p = $c[0]; $req = $c[1]; $note = $c[2]
        $rel = $p; if ($p.StartsWith($root)) { $rel = $p.Substring($root.Length).TrimStart('\') }
        if (Test-Path -LiteralPath $p) {
            if (Test-Path -LiteralPath $p -PathType Leaf) {
                $kb = [math]::Round((Get-Item -LiteralPath $p).Length / 1KB)
                Log ("  [ok]      {0}  ({1:N0} KB)  - {2}" -f $rel, $kb, $note)
            } else {
                $n = @(Get-ChildItem -LiteralPath $p -ErrorAction SilentlyContinue).Count
                Log ("  [ok]      {0}\  ({1} item(s))  - {2}" -f $rel, $n, $note)
            }
        } elseif ($req) {
            Warn ("  [MISSING] {0}  - {1}  (REQUIRED)" -f $rel, $note); $missing++
        } else {
            Warn ("  [absent]  {0}  - {1}  (optional)" -f $rel, $note); $absent++
        }
    }

    if ($missing -gt 0) { Fail "verification failed: $missing required file(s) missing after the install - see the [MISSING] lines in the log. Run Install.bat again with the game disc mounted. If it happens again and an antivirus is active, it may be removing them: check Windows Security > Protection history and add the install folder as an exclusion." }
    if ($absent -gt 0) { Log "  ($absent optional item(s) absent - not fatal, noted above)" }
    $size = (Get-ChildItem -Recurse $root | Where-Object { !$_.PSIsContainer } | Measure-Object -Sum Length).Sum
    Log ("all required files present ({0:N0} MB installed)" -f ($size / 1MB))
}

function Invoke-Uninstall($root) {
    Log "=== UNINSTALL ==="
    $kind = Get-InstallKind $root
    if (!(Test-OurKind $kind)) { Fail ("Not removing $root - it holds " + (Get-KindText $kind) + ". Nothing was removed.") }
    if (Test-Path $root) {
        if (!$ForceOverwrite) { Warn "would remove $root  (re-run with -ForceOverwrite to delete)" }
        elseif ($DryRun) { Log "would remove $root" }
        else {
            $keep = Join-Path ([Environment]::GetFolderPath("MyDocuments")) ("NASCAR_GVR Backup\" + (Get-Date -Format "yyyyMMdd-HHmmss"))
            [void](Backup-PlayerData $root $keep)
            Remove-Item -LiteralPath $root -Recurse -Force; Log "  removed $root"
        }
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
    foreach ($dir in @([Environment]::GetFolderPath("Desktop"), [Environment]::GetFolderPath("Programs"))) {
        foreach ($l in @(Get-ChildItem -LiteralPath $dir -Filter "NASCAR Team Racing*.lnk" -ErrorAction SilentlyContinue)) {
            # only shortcuts that start THIS install
            if ((Test-OurShortcut $l.FullName $root) -and !$DryRun) { Remove-Item -LiteralPath $l.FullName -Force; Log "  removed shortcut $($l.Name)" }
        }
    }
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

# ---- release guard: every file the release zip ships must be here, or nothing is installed ----
# Antivirus (usually Windows Defender) can delete our launcher and DLLs right after the zip is
# extracted: they preload a DLL into the front end and patch code in memory, which heuristics
# flag. Installing anyway gave a half-working game (a plain-shell shortcut, no settings, a hang),
# so stop before touching anything and say which files are gone and why.
# KEEP IN SYNC with $files in Tools\Make-Release.ps1 (everything the installer reads).
$ReleaseFiles = @(
    "Install.bat", "Install-NASCAR-GVR-Portable.ps1",
    "Patches\nascar-bytepatches.txt",
    "Deploy\PLUSDE.dll", "Deploy\GvrSqlite.dll", "Deploy\sqlite3.dll", "Deploy\game.db",
    "Tools\unshield.exe", "Tools\Fix-GvrDirNames.ps1", "Tools\Check-DotNet11.ps1",
    "src\GvrIOShim\build\GvrIO.dll",
    "src\NascarLaunch\build\NascarLaunch.exe", "src\NascarLaunch\nascar_settings.ini", "src\NascarLaunch\NASCAR_GVR.ico",
    "src\GvrSqlite\GvrSqlite.cs",
    "Dependencies\DotNet11\dotnetfx.exe", "Dependencies\DotNet11\NDP1.1sp1-KB867460-X86.exe",
    "Dependencies\DXVK\d3d9.dll"
)
function Assert-ReleaseFiles {
    if ($Uninstall) { return }                      # removing an install must work from any folder
    # A release package (and the NASCAR_GIT repo) always has Install.bat - it is what users run.
    # The dev tree (D:\NFSU_GVR\NASCAR) does not, and finds several of these elsewhere.
    if (!(Test-Path -LiteralPath (Join-Path $SourceRoot "Install.bat"))) {
        Log "release guard skipped: $SourceRoot is not a release package (no Install.bat)"
        return
    }
    $missing = @()
    foreach ($f in $ReleaseFiles) {
        $p = Join-Path $SourceRoot $f
        if (!(Test-Path -LiteralPath $p -PathType Leaf) -or (Get-Item -LiteralPath $p).Length -eq 0) { $missing += $f }
    }
    if ($missing.Count -eq 0) { Log "release files: all $($ReleaseFiles.Count) present in $SourceRoot"; return }
    foreach ($m in $missing) { Warn "  [MISSING from the download] $m" }
    $nl = [Environment]::NewLine
    Fail ("The download is incomplete: " + $missing.Count + " file(s) are missing from" + $nl + $SourceRoot + $nl + $nl +
          ($missing -join $nl) + $nl + $nl +
          "They are in the release zip, so your antivirus most likely removed them after unzipping. " +
          "It is a false alarm: these small files let an arcade game run on a normal PC. Restore them from " +
          "Windows Security > Virus & threat protection > Protection history (or add this folder as an " +
          "exclusion), unzip the release again and run Install.bat. Nothing was installed.")
}
Assert-ReleaseFiles

if ([string]::IsNullOrEmpty($InstallRoot) -and (Test-CanShowDialogs)) {
    while ($true) {
        $pick = Select-Folder "Where should NASCAR Team Racing be installed?`r`nPick a folder or drive (for example C:\Games) - a NASCAR_GVR folder is created inside it." ""
        if (!$pick) { Log "cancelled - nothing was installed"; exit 0 }
        $pick = $pick.TrimEnd("\")
        if ($pick -match '^[A-Za-z]:$') { $InstallRoot = "$pick\NASCAR_GVR" }
        # an existing install of OURS is used as it is (to update it); any other folder - another
        # NASCAR game, another GVR title, the 2.0.2 edition - gets a NASCAR_GVR folder inside it
        elseif (@("ours", "ours-legacy", "ours-partial") -contains (Get-InstallKind $pick)) { $InstallRoot = $pick }
        else { $InstallRoot = Join-Path $pick "NASCAR_GVR" }
        $k = Get-InstallKind $InstallRoot
        if (!(Test-OurKind $k)) {
            Show-Message ("This folder already holds " + (Get-KindText $k) + ":`r`n$InstallRoot`r`n`r`nNothing in it is touched. Pick another folder, for example C:\Games.") "Warning"
            $InstallRoot = $null
            continue
        }
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


# Only ever install into / update a folder that is empty or provably ours (see Get-InstallKind).
$Kind = Get-InstallKind $InstallRoot
if (!(Test-OurKind $Kind)) {
    Fail ("Not installing into $InstallRoot - it already holds " + (Get-KindText $Kind) + ". Nothing in it was touched. Choose an empty folder or C:\Games\NASCAR_GVR.")
}
# Update mode: an existing install of ours is refreshed from this release alone - no game disc, the
# player's settings kept (new ones added). Every stage brings the install to THIS release's state,
# so a player who skipped releases ends up the same as one who took them all. -ForceOverwrite =
# full reinstall from the disc instead.
$UpdateMode = (@("ours", "ours-legacy") -contains $Kind) -and !$ForceOverwrite
$already = $UpdateMode
if ($UpdateMode) {
    $prev = Read-InstallMarker $InstallRoot
    $from = if ($prev -and $prev["Installer"]) { $prev["Installer"] } else { "a release before 1.0.9" }
    Log "UPDATING the existing install in $InstallRoot (from $from) - no game disc needed"
}
else { Log ("install kind: $Kind" + $(if ($ForceOverwrite) { " (-ForceOverwrite: full reinstall)" } else { "" })) }
$PlayerBackup = $null
if ($Kind -ne "empty") {
    $PlayerBackup = Backup-PlayerData $InstallRoot (Join-Path $InstallRoot ("Backup\" + (Get-Date -Format "yyyyMMdd-HHmmss")))
    if (!$DryRun) { Remove-OldBackups $InstallRoot }
}
if ($ResetData) { Warn "-ResetData: the leaderboards/stats database, settings and race saves are reset (backup: $PlayerBackup)" }

if (!$UpdateMode -and !(Test-Path (Join-Path $PayloadRoot "NASCAR\Game\NASCAR_GVR.exe")) -and !$ExtractIfMissing -and !$DryRun) {
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
    # an interrupted earlier install can leave a half-unpacked disc here - always start clean
    if (Test-Path -LiteralPath $ExtractRoot) {
        Log "removing a leftover disc extraction from an earlier run: $ExtractRoot"
        Remove-Item -LiteralPath $ExtractRoot -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $ExtractRoot) { Fail "Could not remove the old disc extraction $ExtractRoot - close any window using it and run Install.bat again." }
    }
}
if (!$UpdateMode -and !$DryRun -and (Test-Path (Join-Path $PayloadRoot "NASCAR\Game\NASCAR_GVR.exe"))) {
    $miss = @(Get-MissingPayloadParts)
    if ($miss.Count) { Fail ("The unpacked game disc at $PayloadRoot is incomplete - missing: " + ($miss -join ", ") + ". Delete that folder and run Install.bat again.") }
}
if (!$UpdateMode -and !(Test-Path (Join-Path $PayloadRoot "NASCAR\Game\NASCAR_GVR.exe"))) {
    if ($ExtractIfMissing) { Extract-Payload }
    else { Fail "Game payload not found at $PayloadRoot. Pass -ExtractIfMissing to unpack it from `"$DiscRoot`"." }
}

$drive = (Split-Path -Qualifier $InstallRoot) + "\"
$free = (Get-PSDrive ($drive[0]) -ErrorAction SilentlyContinue).Free
if ($free -and $free -lt 1GB) { Warn ("only {0:N1} GB free on {1} - about 0.8 GB is needed" -f ($free / 1GB), $drive) }

Ensure-DotNet
if (!$already -or $ForceOverwrite) {
    Install-Payload $Game $Shell $Gvr
    if ($PlayerBackup -and !$ResetData) { Restore-RaceSaves $PlayerBackup $Game }
}
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
Write-InstallMarker $InstallRoot

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
if (!$DryRun) {
    $nl = [Environment]::NewLine
    $msg = "NASCAR Team Racing is " + $(if ($UpdateMode) { "updated" } else { "installed" }) + " in:" + $nl + $InstallRoot + $nl + $nl + "Start it with the NASCAR Team Racing shortcut on your desktop." + $nl + "Settings: nascar_settings.ini in that folder."
    Show-Message $msg
}
