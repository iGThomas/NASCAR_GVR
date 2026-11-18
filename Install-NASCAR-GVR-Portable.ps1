<#
  Install-NASCAR-GVR-Portable.ps1

  Installs GlobalVR "NASCAR Racing" (the v1.1 disc) into a SINGLE user-chosen folder,
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
    [switch]$RepairDotNet,   # unpack the private .NET 1.1 (<install>\Runtime) again even if it works
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
$Version = "2026-11-18-nascar-portable-4.27" # 4.27: release v1.2.2 - menu texture editor Tools\GvrMenuTextures.exe (replacements in Shell\Mods, backed up with the player data). 4.26: release v1.2.2-beta.5 - menus loader glu32.dll (no injection), DDrawCompat helper installed switched off, install-folder check, uninstall removes the graphics-chip choices, settings: Gpu, DDrawCompat, HudText, Preload. 4.25: release v1.2.2-beta.4 - crew chiefs for the 19 extra drivers; nascar_settings.ini in a clearer layout (one note per setting); an update rewrites the player's file in it and keeps every value (Rebuild-Settings). multiplayer (beta): [Multiplayer] Mode=lobby links the arcade menus through an internet relay server (new GvrLinking.dll replaces the IPX original, kept as .oem; NascarLaunch.exe turns the link plug-in on only in lobby mode; NASCAR_Selection.am: the menus' send call restored, cabinet number from the link, Lobby cards show each player's own driver incl. extra drivers; [Multiplayer] PlayerName on the Lobby cards and, through the GvrIO shim, on the cars in the race; GvrIO shim maps the race's -client address to the local link; GvrDriverArt.exe writes single Lobby pictures), Mode=host/join goes straight into a race through the relay or on the same network (rebuilt NascarLaunch.exe, GvrIO.dll, GvrDriverArt.exe); the menus no longer hang on the "game crashed" screen after Alt+F4 or a race crash (Launch_Game_Error.am returns to the attract). [Graphics] AITextureSize defaults to 0 (no size limit for computer cars' paint: the disc's paints are 512 anyway, and a custom paint without mipmaps can no longer crash the race); a settings file still holding beta.3's default 512 under beta.3's comment is switched to 0 once (Update-OldDefaults), a value the player chose is kept. 4.24: release v1.2.2-beta.3 - safe start: before the menus load anything the GvrIO shim prepares COM (one round trip) and starts the install's .NET 1.1 itself on a clean thread ([Launcher] SafeStart, on by default; replaces beta.2's ComWarmUp, a leftover ComWarmUp line does nothing); MOZA R3 bundle steering (a wheel-and-pedals device is no longer taken for a pedal box) (rebuilt GvrIO.dll); community pictures for the extra drivers (new number plates for all 19 and a Kevin Harvick portrait, Deploy\DriverArt -> Shell\Textures\Community; the picture builder uses one while the player's file is still the automatic one or the previous release's, a player's own picture stays; Team Racing portraits for placeholder faces now really used) (rebuilt GvrDriverArt.exe); extra game files (Deploy\GameFiles -> Game, e.g. blue rims for the #2 car: Dodge\2\2-01\2-01rims.bmp, which the race reads after the shared wcrims.bmp; disc files never replaced, list in Game\gvr-gamefiles.txt); [Graphics] race options in nascar_settings.ini: MaxCarDetail (Force LOD A: the installer keeps the disc's car model lists as .gen.oem and makes max-detail ones as .gen.loda, the launcher puts the chosen one in place) and AITextureSize, EnvironmentMap, CompressedTextures, DynamicLighting, MaxVisibleCars written into the race's player profile at every start (rebuilt NascarLaunch.exe). 4.23: release v1.2.2-beta.2 - ATTEMPT against a menus start-up freeze (no window) seen on one PC: a menus library starts .NET inside Windows' DLL-loading lock and a COM call waited there; the GvrIO shim now runs one COM round trip first ([Launcher] ComWarmUp, on by default; not yet confirmed on that PC); freeze capture names the owner of Windows' DLL-loading lock and records deeper stacks (rebuilt GvrIO.dll, NascarLaunch.exe). 4.22: release v1.2.2-beta.1 - shortcut and Programs and Features show the NASCAR Racing logo (NASCAR_Racing.ico); the 19 race cars on the disc without a driver select slot are playable drivers (Harvick, Martin, Nemechek, ...): portraits (placeholders with the name and number; Team Racing's where it is installed), name plates, numbers and leaderboard badges built on this PC from the disc by Tools\GvrDriverArt.exe; players can replace them in Shell\Textures\Faces, Names, Numbers (kept as player data, rebuilt by NascarLaunch.exe at start when changed); [Cabinet] ExtraDrivers=false keeps the 12 originals; leaderboard badge shown for every car, #01 and #1 kept apart (rebuilt NascarLaunch.exe). 4.21: release v1.2.1 - an existing install is found and offered for update (Programs and Features entry, shortcuts, usual folders); each install gets a Programs and Features entry with a working Uninstall (asks first, keeps the player data in Documents; uninstaller copy in <install>\Setup); the script asks Windows for administrator rights itself; the one-file NASCAR-GVR-<version>-Setup.exe runs it. .NET 1.1 is no longer installed into Windows: the installer unpacks .NET 1.1 SP1 into <install>\Runtime (and its libraries next to AMPlayer.exe), run-tests it there, and NascarLaunch.exe starts the menus on that private copy (rebuilt NascarLaunch.exe). Fixes clean PCs where the SP1 package could not even unpack (it needs an older .NET to run) and PCs whose Windows .NET 1.1 was damaged; -RepairDotNet now unpacks the private copy again. 4.20: release v1.2.1-beta.4 - [Display] Renderer now also chooses the NASCAR Racing menus' renderer: auto keeps Direct3D 7 where the game's own test passes and uses OpenGL where it fails (there the game greyed out both and drew in software: a frozen "Please Wait"); with [Debug] Log=true the PC's full DirectX 7 picture and the game's own renderer verdict are logged (rebuilt NascarLaunch.exe, GvrIO.dll). 4.19: release v1.2.1-beta.3 - rebuilt NascarLaunch.exe (Team Racing event-source check; no change for NASCAR Racing). 4.18: release v1.2.1-beta.2 - with [Debug] Log=true the LOG folder also gets the menus' and the race's own messages (debug-output.log), the race engine's traces, Windows' crash records for the game, overlay / hook DLLs loaded into the game and the GPU the menus draw with (rebuilt NascarLaunch.exe, GvrIO.dll). 4.17: release v1.2.1-beta.1 - a generic USB H-shifter is recognised from two held gears and remembered (nascar_calibration.ini [Shifter]), a direct-drive base held by another program is still read for steering, the transmission screen follows a PC wheel found late, the front end never loads NASCAR Team Racing's AKLog2 stand-in, no crash on closing the front end, DirectX device list limited to attached displays and logged (rebuilt GvrIO.dll, NascarLaunch.exe: screens in the PC log). 4.16: release v1.2.0 (NASCAR Racing 1.1.1; one zip with NASCAR Team Racing - Install.bat asks which game) - runs on any Windows region format (the attract never loaded on day/month Windows; the shell now runs as en-US), the game can never restart the PC (nightly restart check patched off in NASCARPlugIn.dll + restart calls blocked), race display modes up to 16384 pixels (5120x1440), wheels with pedals / a gear shifter / a separate rim or button boxes on their own USB cables, set up automatically (pedals and shifter type learned from use, PC pedals calibrated on the arcade's own screen and saved in nascar_calibration.ini (kept as player data); [Wheel] overrides), database password masked in the log, region format in the launcher log (rebuilt GvrIO.dll, NascarLaunch.exe, GvrSqlite.dll). 4.15: default folder NASCAR_GVR_1.1 and desktop/start shortcut "NASCAR Racing" (was "NASCAR Team Racing") so the 1.1 and 2.0.2 editions install side by side without clashing; the old shortcut is migrated on install and uninstall. 4.14: release v1.1.0 - wheel pedals set up automatically by model (rebuilt GvrIO.dll), SwapPedals setting, Car keeps a leading zero (rebuilt NascarLaunch.exe). 4.13: player data (leaderboards/stats db, settings, race saves) backed up before every install/update and kept by -ForceOverwrite (only -ResetData resets it); uninstall keeps a copy in Documents. 4.12: .NET 1.1 must pass a run test (else removed + reinstalled clean, -RepairDotNet forces it); no temp folder from an earlier run is reused (disc extraction, .NET images); unpacked disc checked complete. 4.11: install identity (gvr-install.ini) + update mode (no disc, settings merge); never installs into / updates / removes a folder holding anything else. 4.10: no CLR crash dialog can stall the .NET 1.1 install (regsvcs.exe on Win10). 4.9: release guard - stops before installing when any file of the release zip is missing (antivirus), no plain-shell fallback, launcher required; series default 2005NEXTEL. 4.8: launcher keeps the race above the shell's "Please Wait" overlay in borderless (rebuilt NascarLaunch.exe). 4.7: byte-patch stage logs each file + fails loudly on a non-v1.1 disc; verification logs every required file one by one

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
    try { [void][System.Windows.Forms.MessageBox]::Show($owner, $text, "NASCAR Racing installer", "OK", $icon) }
    finally { $owner.Close() }
}
# can we create files there? (a mounted ISO / DVD drive is read-only - seen in a Windows 7 VM)
# Folders the 2005-2008 programs cannot work from (NascarLaunch.exe checks the same at start, check_folder_name):
# a name with letters outside this Windows' "ANSI" language reaches them as '?' (nothing starts), deep paths pass
# the old 260-character limit, and Program Files / OneDrive / Desktop / Documents block or lock the game's own
# saves (write virtualization, syncing, Windows' ransomware protection). Returns why, or $null when fine.
function Get-UnsafeFolderReason($path) {
    try { $full = [IO.Path]::GetFullPath($path).TrimEnd("\") } catch { return "it is not a valid folder name" }
    $ansi = [Text.Encoding]::Default
    if ($ansi.GetString($ansi.GetBytes($full)) -ne $full) { return "its name has letters this old game cannot read (use plain Latin letters, numbers, spaces and _ - .)" }
    if ($full.Length -gt 150) { return "its path is too long for this old game (keep it under 150 characters)" }
    $places = @(
        @([Environment]::GetFolderPath("ProgramFiles"), "Program Files - Windows blocks the game's own saves there"),
        @([Environment]::GetFolderPath("ProgramFilesX86"), "Program Files - Windows blocks the game's own saves there"),
        @($env:OneDrive, "OneDrive - syncing locks the game's files"),
        @($env:OneDriveConsumer, "OneDrive - syncing locks the game's files"),
        @($env:OneDriveCommercial, "OneDrive - syncing locks the game's files"),
        @([Environment]::GetFolderPath("Desktop"), "the Desktop - often synced, and Windows' ransomware protection can block the game"),
        @([Environment]::GetFolderPath("MyDocuments"), "Documents - Windows' ransomware protection can block the game's saves"),
        @([Environment]::GetFolderPath("Windows"), "the Windows folder"))
    foreach ($p in $places) {
        if ($p[0] -and ($full + "\").StartsWith(([string]$p[0]).TrimEnd("\") + "\", [StringComparison]::OrdinalIgnoreCase)) { return "it is inside " + $p[1] }
    }
    return $null
}

# Tools\GvrMenuTextures.exe: players see and replace the menus' textures with it. Replacements are kept in
# Shell\Mods (player data); NascarLaunch.exe puts them into the scenes at every start.
function Install-MenuTextures($root) {
    $src = Join-Path $SourceRoot "src\MenuTextures\GvrMenuTextures.exe"
    if (!(Test-Path -LiteralPath $src)) { Warn "GvrMenuTextures.exe missing from the package - the menu texture editor will not be available"; return }
    $tools = Join-Path $root "Tools"
    if ($DryRun) { Log "  would copy GvrMenuTextures.exe to $tools"; return }
    New-Item -ItemType Directory -Force $tools | Out-Null
    Copy-Item -LiteralPath $src -Destination (Join-Path $tools "GvrMenuTextures.exe") -Force
    Log "--- menu texture editor installed (Tools\GvrMenuTextures.exe; replacements in Shell\Mods) ---"
}

# glu32.dll of ours next to the menus (Shell\bin): the menus then load GvrIO.dll by themselves, before their first
# instruction, instead of NascarLaunch.exe injecting it (some antivirus programs block injection). The launcher falls
# back to injection when this file is missing or did not work. A foreign glu32.dll there is kept as glu32.dll.oem.
function Install-GvrGlu($shell) {
    $src = Join-Path $SourceRoot "src\GvrGlu\build\glu32.dll"
    if (!(Test-Path -LiteralPath $src)) { Warn "glu32.dll (menus loader) missing from the package - the launcher injects GvrIO.dll instead"; return }
    $live = Join-Path $shell "bin\glu32.dll"
    if ($DryRun) { Log "  would install $live"; return }
    if ((Test-Path -LiteralPath $live) -and
        !([IO.File]::ReadAllText($live, [Text.Encoding]::GetEncoding(28591)).Contains("GvrGlu: loads GvrIO.dll into the menus"))) {
        Copy-Item -LiteralPath $live -Destination "$live.oem" -Force
    }
    Copy-Item -LiteralPath $src -Destination $live -Force
    Log "--- menus loader installed (Shell\bin\glu32.dll: the menus load GvrIO.dll themselves, no injection) ---"
}

# DDrawCompat (open source, 0BSD): a compatibility helper for the menus' old Direct3D. Installed switched OFF as
# Shell\bin\ddraw-compat.dll; NascarLaunch.exe copies it to Shell\bin\ddraw.dll only when [Display] DDrawCompat
# asks for it (auto = when the PC fails the menus' graphics tests). An active copy of an older version is removed
# first, so the launcher never mistakes it for a foreign ddraw.dll.
function Install-DDrawCompat($shell) {
    $src = Join-Path $SourceRoot "Deploy\DDrawCompat\ddraw.dll"
    if (!(Test-Path -LiteralPath $src)) { Warn "DDrawCompat missing from the package - the menus' Direct3D helper will not be available"; return }
    $bin = Join-Path $shell "bin"
    $off = Join-Path $bin "ddraw-compat.dll"; $live = Join-Path $bin "ddraw.dll"
    if ($DryRun) { Log "  would install DDrawCompat as $off (switched off)"; return }
    if ((Test-Path -LiteralPath $live) -and (Test-Path -LiteralPath $off) -and ((Get-Item -LiteralPath $live).Length -eq (Get-Item -LiteralPath $off).Length)) {
        Remove-Item -LiteralPath $live -Force -ErrorAction SilentlyContinue
        Log "  DDrawCompat: the active copy of the previous version removed (the launcher switches it on again when needed)"
    }
    Copy-Item -LiteralPath $src -Destination $off -Force
    $lic = Join-Path $SourceRoot "Deploy\DDrawCompat\LICENSE.txt"
    if (Test-Path -LiteralPath $lic) { Copy-Item -LiteralPath $lic -Destination (Join-Path $bin "ddraw-compat-LICENSE.txt") -Force }
    Log "--- DDrawCompat installed (switched off; used only when this PC fails the menus' graphics tests) ---"
}

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
# Default sub-folder and shortcut names, kept DISTINCT from the 2.0.2 "NASCAR Team Racing"
# edition so both install side by side without clashing (user, 2026-10-23).
$FolderLeaf         = "NASCAR_GVR_1.1"       # created inside the folder/drive the player picks
$ShortcutName       = "NASCAR Racing"        # the 2.0.2 edition uses "NASCAR Team Racing"
$IconFile           = "NASCAR_Racing.ico"   # shortcut + Programs and Features icon (copied into the install)
$LegacyShortcutName = "NASCAR Team Racing"   # 1.1 used this before 4.15 - migrated on install/uninstall
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
    $ours = @("Game", "Shell", "GVR", "LOG", "Fonts", "Runtime", "Setup", "Backup", "Tools", "NascarLaunch.exe", "nascar_settings.ini",
              "nascar_registry.ini", "nascar_calibration.ini", "NASCAR_GVR.ico", $IconFile, $MarkerName)
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
            "Package=" + (Get-PackageVersion) + $nl +
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

# ---- existing installs, Programs and Features, uninstall ----------------------------------------
# Updating must take no searching: the installer finds this edition's existing installs itself - the
# Programs and Features entries it writes (since v1.2.1), the desktop / Start-menu shortcuts and the
# usual folders (<drive>:\Games\<FolderLeaf> or NASCAR_GVR) - and offers to update the one it finds.
# Only folders Get-InstallKind proves to be this product and edition count.
# Each install gets its own Programs and Features entry (key NASCAR_GVR_<edition>_<folder tag>) with a
# working Uninstall: a copy of this script in <install>\Setup, run with -Uninstall; it asks first,
# copies the player's data to Documents and removes the folder, its shortcuts and its entry.
$UninstallKeyBase = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"
# entries are written to the 64-bit view; the 32-bit one is searched too (a 32-bit PowerShell writes
# there - the first Setup.exe build started that one) so an install is never listed twice
$UninstallKeyViews = @($UninstallKeyBase, "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall")
$UninstallPrefix  = "NASCAR_GVR_$Edition"
$SetupDirName     = "Setup"
$DisplayTitle     = "$ShortcutName (GlobalVR arcade)"

function Get-PathTag($root) {
    $b = [Text.Encoding]::UTF8.GetBytes($root.TrimEnd('\').ToLowerInvariant())
    return ([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($b), 0, 4)).Replace("-", "")
}

function Find-ExistingInstalls {
    $cand = New-Object System.Collections.ArrayList
    foreach ($k in @(Get-ChildItem $UninstallKeyViews -ErrorAction SilentlyContinue | Where-Object { $_.PSChildName -like "$UninstallPrefix*" })) {
        $loc = (Get-ItemProperty -Path $k.PSPath -ErrorAction SilentlyContinue).InstallLocation
        if ($loc) { [void]$cand.Add($loc) }
    }
    $wsh = $null; try { $wsh = New-Object -ComObject WScript.Shell } catch {}
    foreach ($dir in @([Environment]::GetFolderPath("Desktop"), [Environment]::GetFolderPath("CommonDesktopDirectory"),
                       [Environment]::GetFolderPath("Programs"), [Environment]::GetFolderPath("CommonPrograms"))) {
        foreach ($n in @(@($ShortcutName, $LegacyShortcutName) | Select-Object -Unique)) {
            $l = Join-Path $dir "$n.lnk"
            if (!$wsh -or !(Test-Path -LiteralPath $l)) { continue }
            try { $t = $wsh.CreateShortcut($l).TargetPath; if ($t) { [void]$cand.Add((Split-Path -Parent $t)) } } catch {}
        }
    }
    foreach ($d in [IO.DriveInfo]::GetDrives()) {
        if ($d.DriveType -ne "Fixed" -or !$d.IsReady) { continue }
        foreach ($leaf in @($FolderLeaf, "NASCAR_GVR")) { [void]$cand.Add((Join-Path $d.RootDirectory.FullName "Games\$leaf")) }
    }
    $seen = @{}
    foreach ($c in $cand) {
        $r = ([string]$c).TrimEnd('\')
        if (!$r -or $seen.ContainsKey($r.ToLowerInvariant())) { continue }
        $seen[$r.ToLowerInvariant()] = $true
        if (!(Test-Path -LiteralPath $r -PathType Container)) { continue }
        if (@("ours", "ours-legacy") -contains (Get-InstallKind $r)) { $r }
    }
}

function Ask-YesNo($text) {
    if (!(Test-CanShowDialogs)) { return $null }
    $owner = New-DialogOwner
    try { return ([System.Windows.Forms.MessageBox]::Show($owner, $text, "$ShortcutName installer", "YesNo", "Question") -eq "Yes") }
    finally { $owner.Close() }
}

# The release number of the package this script came from (Make-Release writes it into README.txt).
function Get-PackageVersion {
    $r = Join-Path $SourceRoot "README.txt"
    if (Test-Path -LiteralPath $r) {
        $l = Get-Content -LiteralPath $r -TotalCount 1
        if ($l -match '^NASCAR GVR (\S+)') { return $Matches[1] }
    }
    if ($InstallRoot) { $m = Read-InstallMarker $InstallRoot; if ($m -and $m["Package"]) { return $m["Package"] } }
    return ($Version -replace '^.*-portable-', 'installer ')
}

function Register-Uninstall($root) {
    if ($DryRun) { Log "would add '$DisplayTitle' to Programs and Features"; return }
    $setup = Join-Path $root $SetupDirName
    New-Dir $setup
    $dst = Join-Path $setup (Split-Path -Leaf $PSCommandPath)
    if ($PSCommandPath -ne $dst) { Copy-Item -LiteralPath $PSCommandPath -Destination $dst -Force }
    $key = Join-Path $UninstallKeyBase ($UninstallPrefix + "_" + (Get-PathTag $root))
    if (!(Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
    $ps = Join-Path $env:windir "System32\WindowsPowerShell\v1.0\powershell.exe"
    $ver = Get-PackageVersion
    $kb = [int](((Get-ChildItem -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue | Where-Object { !$_.PSIsContainer } | Measure-Object Length -Sum).Sum) / 1KB)
    $text = [ordered]@{
        DisplayName     = $DisplayTitle
        DisplayVersion  = $ver
        Publisher       = "NASCAR GVR project"
        InstallLocation = $root
        DisplayIcon     = (Join-Path $root $IconFile)
        UninstallString = "`"$ps`" -NoProfile -STA -ExecutionPolicy Bypass -File `"$dst`" -Uninstall -ForceOverwrite -InstallRoot `"$root`""
        URLInfoAbout    = "https://github.com/iGThomas/NASCAR_GVR"
        HelpLink        = "https://github.com/iGThomas/NASCAR_GVR/issues"
        Comments        = "Installer $Version"
        InstallDate     = (Get-Date).ToString("yyyyMMdd")
    }
    foreach ($n in $text.Keys) { New-ItemProperty -Path $key -Name $n -Value $text[$n] -PropertyType String -Force | Out-Null }
    foreach ($n in "NoModify", "NoRepair") { New-ItemProperty -Path $key -Name $n -Value 1 -PropertyType DWord -Force | Out-Null }
    New-ItemProperty -Path $key -Name EstimatedSize -Value $kb -PropertyType DWord -Force | Out-Null
    Unregister-Uninstall $root (Get-Item $key).Name          # an older duplicate for this folder
    Log "Programs and Features: '$DisplayTitle' $ver ($([int]($kb / 1024)) MB), Uninstall runs $dst"
}

function Unregister-Uninstall($root, $except = "") {
    foreach ($k in @(Get-ChildItem $UninstallKeyViews -ErrorAction SilentlyContinue | Where-Object { $_.PSChildName -like "$UninstallPrefix*" })) {
        $loc = (Get-ItemProperty -Path $k.PSPath -ErrorAction SilentlyContinue).InstallLocation
        if ($except -and $k.Name -ieq $except) { continue }
        if ($loc -and $loc.TrimEnd('\') -ieq $root.TrimEnd('\')) {
            if ($DryRun) { Log "  would remove the Programs and Features entry $($k.PSChildName)" }
            else { Remove-Item -Path $k.PSPath -Recurse -Force; Log "  removed the Programs and Features entry $($k.PSChildName)" }
        }
    }
}

# Settings merge on update: add every section / setting the new template has and the player's
# nascar_settings.ini lacks (with the comment lines just above it), and never change a value the
# player has. Unknown keys of theirs are left alone. PowerShell 2.0 compatible.
# One-time change of a default the player never chose: a key still at an OLD default, with the comment line
# an earlier release wrote right above it, gets the new value and the new comment. A value the player changed
# keeps its own number; one set back later sits under the new comment and is never changed again.
$OldDefaults = @(
    @{ Key = "AITextureSize"; Old = "512"; New = "0"     # 1.2.2-beta.3 wrote 512 (the arcade's); 0 from beta.4
       OldComment = "; Computer cars' paint size limit: 512, or 0 = none (also stops custom paint crashing on them)."
       NewComment = "; Largest paint the computer cars may use, in pixels. 0 = no limit." }
)
function Update-OldDefaults($ini) {
    if (!(Test-Path -LiteralPath $ini)) { return }
    $lines = [IO.File]::ReadAllLines($ini); $n = 0
    for ($i = 1; $i -lt $lines.Count; $i++) {
        foreach ($d in $OldDefaults) {
            if ($lines[$i].Trim() -eq ($d.Key + "=" + $d.Old) -and $lines[$i - 1].Trim() -eq $d.OldComment) {
                $lines[$i - 1] = $d.NewComment; $lines[$i] = $d.Key + "=" + $d.New; $n++
                Log ("  settings: " + $d.Key + " " + $d.Old + " -> " + $d.New + " (new default; it was never changed by you)")
            }
        }
    }
    if ($n -and !$DryRun) { [IO.File]::WriteAllLines($ini, $lines) }
}

# Settings on an update: the file is written again in the current layout (new notes, new settings with
# their defaults) and every value the player set is carried over - also lines the template does not have
# (e.g. wheel ButtonN), kept at the end of their section under "Your own settings". Runs after
# Update-OldDefaults, which needs the old notes to recognise an untouched old default.
function Rebuild-Settings($dst, $tpl) {
    if (!(Test-Path -LiteralPath $tpl) -or !(Test-Path -LiteralPath $dst)) { return }
    $vals = @{}; $order = @{}; $secOrder = New-Object System.Collections.ArrayList; $sec = ""
    foreach ($raw in [IO.File]::ReadAllLines($dst)) {
        $l = $raw.Trim()
        if ($l -match '^\[(.+)\]$') { $sec = $Matches[1].Trim(); $sk = $sec.ToLower()
            if (!$vals.ContainsKey($sk)) { $vals[$sk] = @{}; $order[$sk] = New-Object System.Collections.ArrayList; [void]$secOrder.Add($sec) }; continue }
        if (!$l -or $l.StartsWith(";") -or $l.StartsWith("#") -or !$l.Contains("=")) { continue }
        $sk = $sec.ToLower()
        if (!$vals.ContainsKey($sk)) { $vals[$sk] = @{}; $order[$sk] = New-Object System.Collections.ArrayList; [void]$secOrder.Add($sec) }
        $i = $l.IndexOf("="); $k = $l.Substring(0, $i).Trim(); $v = $l.Substring($i + 1).Trim()
        if (!$vals[$sk].ContainsKey($k.ToLower())) { [void]$order[$sk].Add($k) }
        $vals[$sk][$k.ToLower()] = $v
    }
    $out = New-Object System.Collections.ArrayList; $used = @{}; $tplSecs = @{}; $sec = ""; $kept = 0
    $flush = {
        param($s)
        if (!$s -or !$vals.ContainsKey($s)) { return }
        $extra = @($order[$s] | Where-Object { !$used.ContainsKey($s + "|" + $_.ToLower()) })
        if ($extra.Count) {
            [void]$out.Add("; Your own settings:")
            foreach ($k in $extra) { [void]$out.Add($k + " = " + $vals[$s][$k.ToLower()]) }
            [void]$out.Add("")
        }
    }
    foreach ($raw in [IO.File]::ReadAllLines($tpl)) {
        $l = $raw.Trim()
        if ($l -match '^\[(.+)\]$') { & $flush $sec; $sec = $Matches[1].Trim().ToLower(); $tplSecs[$sec] = 1; [void]$out.Add($raw); continue }
        if ($l -and !$l.StartsWith(";") -and !$l.StartsWith("#") -and $l.Contains("=")) {
            $i = $raw.IndexOf("="); $kl = $raw.Substring(0, $i).Trim().ToLower()
            if ($vals.ContainsKey($sec) -and $vals[$sec].ContainsKey($kl)) {
                $v = $vals[$sec][$kl]; $used[$sec + "|" + $kl] = 1; $kept++
                [void]$out.Add($raw.Substring(0, $i + 1) + $(if ($v) { " " + $v } else { "" })); continue
            }
        }
        [void]$out.Add($raw)
    }
    & $flush $sec
    foreach ($s in $secOrder) {                         # whole sections the template does not have
        if ($tplSecs.ContainsKey($s.ToLower()) -or !$order[$s.ToLower()].Count) { continue }
        [void]$out.Add(""); [void]$out.Add("[" + $s + "]"); [void]$out.Add("; Your own settings:")
        foreach ($k in $order[$s.ToLower()]) { [void]$out.Add($k + " = " + $vals[$s.ToLower()][$k.ToLower()]) }
    }
    if ($DryRun) { Log "  settings: would rewrite nascar_settings.ini in the new layout ($kept of your values kept)"; return }
    [IO.File]::WriteAllLines($dst, [string[]]$out.ToArray())
    Log "  settings: nascar_settings.ini rewritten in the new layout - $kept of your values kept, new settings at their defaults"
}

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
# nascar_registry.ini; the pedal calibration in nascar_calibration.ini; the race's
# profile/controls/garage in Game\Save. An update never replaces
# them; a -ForceOverwrite reinstall keeps them too (only -ResetData resets them). Before every
# install/update they are copied to <install>\Backup\<time> (newest 5 kept), and uninstall copies
# them to Documents\NASCAR_GVR Backup before the folder is removed.
$PlayerDataItems = @("GVR\GvrPlus\game.db", "nascar_settings.ini", "nascar_registry.ini", "nascar_calibration.ini", "Game\Save",
                     "Shell\Textures\Faces", "Shell\Textures\Names", "Shell\Textures\Numbers", "Shell\Mods")
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
# dongle storage ABI (the GVRStorageDevice vtable).
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

# The menus' cabinet link (GvrLinking.dll in Shell\bin): the original links cabinets over IPX, which
# Windows no longer has, and hangs the menus when its plug-in is on (Disable-CabinetPlugIns keeps the
# plug-in off). Ours links them through the internet relay instead; NascarLaunch.exe turns the plug-in
# on only with [Multiplayer] Mode=lobby, so single-player menus do not change. OEM kept as .oem.
function Install-GvrLinking($shell) {
    $src = Join-Path $SourceRoot "src\GvrLinking\build\GvrLinking.dll"
    if (!(Test-Path -LiteralPath $src)) { Warn "GvrLinking.dll (relay link) missing from the package - the arcade lobby will not be available"; return }
    $live = Join-Path $shell "bin\GvrLinking.dll"
    $oem  = Join-Path $shell "bin\GvrLinking.dll.oem"
    Log "--- menus' cabinet link (arcade lobby over the internet relay) ---"
    if ($DryRun) { Log "  would install $live (OEM kept as $oem)"; return }
    # only treat the current file as OEM if it is not already ours
    if ((Test-Path -LiteralPath $live) -and !(Test-Path -LiteralPath $oem) -and
        !([IO.File]::ReadAllText($live, [Text.Encoding]::GetEncoding(28591)).Contains("relay link for the menus"))) {
        Copy-Item -LiteralPath $live -Destination $oem -Force
    }
    Copy-FileIfPresent $src $live "GvrLinking.dll (relay link)" | Out-Null
    Log "  installed; the lobby is used only with [Multiplayer] Mode=lobby"
}

# Support DLLs are staged on EVERY run (idempotent), not just during the first
# copy, so re-running the installer repairs or adds them to an existing install.
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
# break a relocated install.
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

# Dependencies are large (100 MB DirectX redist, 35 MB .NET 1.1); use the package's own
# Dependencies folder (or -DependenciesRoot) rather than duplicating them.
function Resolve-Dependencies {
    if (![string]::IsNullOrEmpty($DependenciesRoot)) { return $DependenciesRoot }
    foreach ($c in @((Join-Path $SourceRoot "Dependencies"))) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

# ---- .NET Framework 1.1: a private copy inside the install, nothing installed into Windows -------
# The menus (AMPlayer.exe + PLUSDE) need the REAL CLR 1.1, never 2.0 or later: PLUSDE's mixed-mode
# code uses native C++ exceptions as control flow and that interop access-violates on 2.0.
# Until installer 4.20 / 1.4, .NET 1.1 was installed into Windows (SP1 slipstreamed into an admin
# image - the plain redist fails with MSI 1603 on Windows 10/11). That kept failing on players' PCs
# in ways only a clean reinstall fixed: a damaged 1.1 hung the menus with no window, a regsvcs.exe
# crash dialog stalled the silent install, and the SP1 package's unpacker is itself a .NET 1.x
# program, so on a PC with no older .NET it cannot run at all (0x80131700, seen on a clean
# Windows 10, 2026-11-11). Since 4.21 / 1.5 the runtime is unpacked into <install>\Runtime\v1.1.4322
# instead, and NascarLaunch.exe points the menus at it (COMPLUS_InstallRoot + COMPLUS_Version,
# which Windows' own mscoree honours - verified on a Windows 10 with no .NET 1.1, both games):
#   1. dotnetfx.exe /c /t: unpacks netfx.msi, msiexec /a turns it into a file image (installs nothing)
#   2. the SP1 package carries every updated file whole in its largest embedded cabinet: Windows'
#      expand.exe unpacks it and each file replaces the file it updates (-> 1.1.4322.2032)
#   3. .NET's own libraries are copied next to AMPlayer.exe (a private runtime has no global
#      assembly cache - a missing one stops the menus at a binding-error box), then a small 1.1
#      program must compile and run on the result.
# The result matches a real SP1 install file for file, apart from tools (gacutil, the updater) and
# the security policy files .NET writes on first use. An existing private copy that passes the run
# test is kept; one that fails is unpacked again (-RepairDotNet forces that). A .NET 1.1 already in
# Windows is left alone and no longer used by the game.
$PrivateNetVersion = "v1.1.4322"
$PrivateNetBuild   = "1.1.4322.2032"   # SP1

# During the run test CLR 1.1 must not show its "Application has generated an exception that could
# not be handled" JIT-debug dialog and wait for a click (a crashed regsvcs.exe stalled the old silent
# install that way, 2026-10-11): DbgJITDebugLaunchSetting = 1 ends a crashed process without asking.
# Set in both registry views (CLR 1.1 is 32-bit: WOW6432Node on 64-bit Windows) and put back
# afterwards - removed again where it did not exist. Returns whatever $Body outputs.
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
    try { & $Body }
    finally {
        foreach ($s in $saved) {
            try {
                if ($null -eq $s[1]) { Remove-ItemProperty -Path $s[0] -Name DbgJITDebugLaunchSetting -ErrorAction SilentlyContinue }
                else { New-ItemProperty -Path $s[0] -Name DbgJITDebugLaunchSetting -PropertyType DWord -Value $s[1] -Force | Out-Null }
            } catch {}
        }
    }
}

function Reset-Dir($p) {
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $p) { Fail "Could not clear the folder $p - close the game and any window using it, then run Install.bat again." }
    New-Item -ItemType Directory -Force $p | Out-Null
}

# Runs $Body with the private runtime selected the way NascarLaunch.exe selects it; restores the
# two variables afterwards.
function Invoke-WithPrivateNet($rt, [scriptblock]$Body) {
    $old = @($env:COMPLUS_InstallRoot, $env:COMPLUS_Version)
    $env:COMPLUS_InstallRoot = Split-Path -Parent $rt
    $env:COMPLUS_Version = $PrivateNetVersion
    try { & $Body }
    finally {
        if ($null -eq $old[0]) { Remove-Item env:COMPLUS_InstallRoot -ErrorAction SilentlyContinue } else { $env:COMPLUS_InstallRoot = $old[0] }
        if ($null -eq $old[1]) { Remove-Item env:COMPLUS_Version -ErrorAction SilentlyContinue } else { $env:COMPLUS_Version = $old[1] }
    }
}

# $null when the private runtime in $rt works, else the reason. A tiny 1.1 program doing what the
# menus' .NET does first (culture + number/currency/date formatting, System.Data) must compile with
# the runtime's own compiler and run on it within 30 s.
function Test-PrivateDotNet($rt) {
    foreach ($f in "mscorlib.dll", "mscorwks.dll", "System.dll", "System.Data.dll", "System.Xml.dll", "csc.exe") {
        if (!(Test-Path -LiteralPath (Join-Path $rt $f))) { return "$f is missing" }
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
    foreach ($f in "System.dll", "System.Data.dll", "System.Xml.dll") { Copy-Item -LiteralPath (Join-Path $rt $f) -Destination $t }   # no global assembly cache
    $out = Join-Path $t "out.txt"
    return Invoke-WithPrivateNet $rt {
        $p = Start-Process (Join-Path $rt "csc.exe") -ArgumentList "/nologo /out:`"$t\P.exe`" /r:System.Data.dll `"$t\P.cs`"" -WorkingDirectory $t `
                           -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $t "csc.txt")
        if ($p.ExitCode -ne 0 -or !(Test-Path (Join-Path $t "P.exe"))) {
            $msg = if (Test-Path (Join-Path $t "csc.txt")) { (Get-Content (Join-Path $t "csc.txt") | Select-Object -First 2) -join " " } else { "" }
            return "its C# compiler failed (exit $($p.ExitCode)) $msg"
        }
        $why = Invoke-QuietClrCrashes {
            $q = Start-Process (Join-Path $t "P.exe") -WorkingDirectory $t -WindowStyle Hidden -PassThru -RedirectStandardOutput $out
            $null = $q.Handle   # keep the handle: without it ExitCode is empty after WaitForExit(timeout)
            if (!$q.WaitForExit(30000)) {
                try { $q.Kill() } catch { }
                "a .NET 1.1 test program hung (no exit within 30 s) - the same hang the menus would show"
            }
            elseif ($q.ExitCode -ne 42) { "a .NET 1.1 test program crashed (exit $($q.ExitCode))" }
        }
        if ($why) { return $why }
        $line = if (Test-Path $out) { (Get-Content $out | Select-Object -First 1) } else { "" }
        if ($line -notlike "1.1.4322*") { return "the test program did not run on .NET 1.1 (it reported '$line')" }
        Log "  .NET 1.1 test: $line"
        return $null
    }
}

# The SP1 package (NDP1.1sp1-KB867460-X86.exe) embeds three Microsoft cabinets ("MSCF" header,
# cabinet size at +8); the largest holds the updated files. Writes it out and unpacks it into $dest.
function Expand-Sp1Files($sp1, $work, $dest) {
    $bytes = [IO.File]::ReadAllBytes($sp1)
    $text = [Text.Encoding]::GetEncoding(28591).GetString($bytes)   # one char per byte
    $best = $null; $at = 0
    while (($i = $text.IndexOf("MSCF`0`0`0`0", $at, [StringComparison]::Ordinal)) -ge 0) {
        $size = [BitConverter]::ToUInt32($bytes, $i + 8)
        if ($i + $size -le $bytes.Length -and (!$best -or $size -gt $best[1])) { $best = @($i, [int]$size) }
        $at = $i + 4
    }
    if (!$best) { Fail "The .NET 1.1 SP1 package has no cabinet inside - the file is damaged. Unzip the release again." }
    $cab = Join-Path $work "sp1.cab"
    $fs = [IO.File]::Create($cab); try { $fs.Write($bytes, $best[0], $best[1]) } finally { $fs.Close() }
    $p = Start-Process (Join-Path $env:windir "System32\expand.exe") -ArgumentList "-F:* `"$cab`" `"$dest`"" -WindowStyle Hidden -Wait -PassThru
    $n = @(Get-ChildItem -LiteralPath $dest -File).Count
    if ($p.ExitCode -ne 0 -or $n -lt 50) { Fail ".NET 1.1 SP1 files could not be unpacked (expand.exe exit $($p.ExitCode), $n file(s))" }
    Log ("  SP1: {0} updated file(s) unpacked ({1:N1} MB cabinet)" -f $n, ($best[1] / 1MB))
}

# Each SP1 file is named after its Windows Installer file key - "FL_System_Data_dll_____X86.<GUID>",
# "mscorwks_dll_4_____X86.<GUID>", keys cut at 72 characters - and replaces every runtime file whose
# name reads the same with "." as "_". Files SP1 adds that the game never uses (mscoree.dll and
# mscorier.dll of Windows' own folder, ASP.NET SQL templates) have no counterpart and are skipped.
function Update-FromSp1($rt, $files) {
    $byName = @{}
    foreach ($f in Get-ChildItem -LiteralPath $rt -Recurse -File) {
        $k = $f.Name.ToLowerInvariant().Replace('.', '_')
        if (!$byName.ContainsKey($k)) { $byName[$k] = @() }
        $byName[$k] += $f.FullName
    }
    $n = 0; $skipped = @()
    foreach ($s in Get-ChildItem -LiteralPath $files -File) {
        if ($s.Name -notmatch '^(?:FL_)?(?<stem>.+?)_{2,}(?:X86)?\.[0-9A-Fa-f]{8}_') { $skipped += $s.Name; continue }
        $stem = $Matches.stem.ToLowerInvariant().Replace('.', '_')
        $hits = $byName[$stem]
        if (!$hits) { $hits = $byName[($stem -replace '_\d+$', '')] }
        if (!$hits) { $k = @($byName.Keys | Where-Object { $_.StartsWith($stem) }); if ($k.Count -eq 1) { $hits = $byName[$k[0]] } }
        if (!$hits) { $skipped += $s.Name; continue }
        foreach ($h in $hits) { Copy-Item -LiteralPath $s.FullName -Destination $h -Force; $n++ }
    }
    Log "  SP1: $n runtime file(s) updated, $($skipped.Count) not used by the game skipped"
}

# .NET's own libraries next to AMPlayer.exe. A file of the same name is replaced only when it is
# .NET's (Microsoft's two framework key tokens), never when it belongs to the game.
function Copy-FrameworkToShell($rt, $bin) {
    $msKeys = @("B7-7A-5C-56-19-34-E0-89", "B0-3F-5F-7F-11-D5-0A-3A")
    $n = 0; $kept = 0
    foreach ($f in Get-ChildItem -LiteralPath $rt -Filter *.dll) {
        if ($f.Name -ieq "mscorlib.dll") { continue }   # always loaded from the runtime folder itself
        try { [void][Reflection.AssemblyName]::GetAssemblyName($f.FullName) } catch { continue }   # a native DLL
        $dst = Join-Path $bin $f.Name
        if (Test-Path -LiteralPath $dst) {
            $tok = ""
            try { $tok = [BitConverter]::ToString([Reflection.AssemblyName]::GetAssemblyName($dst).GetPublicKeyToken()) } catch {}
            if ($msKeys -notcontains $tok) { $kept++; continue }
            if ((Get-Item -LiteralPath $dst).Length -eq $f.Length -and (Get-Item -LiteralPath $dst).VersionInfo.FileVersion -eq $f.VersionInfo.FileVersion) { continue }
        }
        Copy-Item -LiteralPath $f.FullName -Destination $dst -Force; $n++
    }
    Log ("  .NET libraries next to AMPlayer.exe: $n copied" + $(if ($kept) { ", $kept game file(s) of the same name kept" } else { "" }))
}

function Install-PrivateDotNet($root, $shell) {
    if ($SkipDotNet -or $SkipShell) { return }
    Log "--- .NET 1.1 for the menus: private copy in this install (nothing installed into Windows) ---"
    $rt = Join-Path $root "Runtime\$PrivateNetVersion"
    $bin = Join-Path $shell "bin"
    if ($DryRun) { Log "  would unpack .NET 1.1 SP1 into $rt and copy its libraries next to AMPlayer.exe"; return }
    if ((Test-Path -LiteralPath (Join-Path $rt "mscorwks.dll")) -and !$RepairDotNet) {
        $why = Test-PrivateDotNet $rt
        if (!$why) { Log "  present and working: $rt"; Copy-FrameworkToShell $rt $bin; return }
        Warn "  the private .NET 1.1 does not work ($why) - unpacking it again"
    }
    elseif ($RepairDotNet) { Log "  -RepairDotNet: unpacking it again" }

    $dep = Resolve-Dependencies
    $dfx = if ($dep) { Join-Path $dep "DotNet11\dotnetfx.exe" } else { "" }
    $sp1 = if ($dep) { Join-Path $dep "DotNet11\NDP1.1sp1-KB867460-X86.exe" } else { "" }
    foreach ($f in $dfx, $sp1) {
        if (!$f -or !(Test-Path -LiteralPath $f)) { Fail "The .NET 1.1 package (Dependencies\DotNet11\dotnetfx.exe and NDP1.1sp1-KB867460-X86.exe) is missing from this folder - unzip the release again." }
    }
    $w = Join-Path $LogDir "dotnet11"; $adm = Join-Path $LogDir "dotnet11_image"; $sp = Join-Path $LogDir "dotnet11_sp1"
    Reset-Dir $w; Reset-Dir $adm; Reset-Dir $sp                    # never anything left by an earlier run
    $p = Start-Process $dfx -ArgumentList "/q /c /t:`"$w`"" -Wait -PassThru
    if (!(Test-Path (Join-Path $w "netfx.msi"))) { Fail "dotnetfx.exe could not be unpacked (exit $($p.ExitCode))" }
    $p = Start-Process msiexec -ArgumentList "/a `"$w\netfx.msi`" TARGETDIR=`"$adm`" /qn" -Wait -PassThru
    $urt = Get-ChildItem -LiteralPath $adm -Recurse -Filter mscorwks.dll -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$urt) { Fail ".NET 1.1 could not be unpacked to files (msiexec /a exit $($p.ExitCode))" }
    $urt = $urt.DirectoryName
    Log "  unpacked .NET 1.1 ($((Get-Item (Join-Path $urt 'mscorwks.dll')).VersionInfo.FileVersion))"
    Expand-Sp1Files $sp1 $w $sp

    $new = "$rt.new"
    Reset-Dir $new
    foreach ($i in Get-ChildItem -LiteralPath $urt) {
        if ($i.Name -ieq "GAC") { continue }                        # the image's assembly-cache copies: not used
        Copy-Item -LiteralPath $i.FullName -Destination $new -Recurse -Force
    }
    Remove-Item -LiteralPath (Join-Path $new "gdiplus.dll") -Force -ErrorAction SilentlyContinue   # a real install leaves it out (Windows has its own)
    Update-FromSp1 $new $sp
    foreach ($f in "mscorwks.dll", "mscorlib.dll", "System.dll", "System.Data.dll", "System.Xml.dll", "System.Windows.Forms.dll", "System.Drawing.dll") {
        $v = (Get-Item -LiteralPath (Join-Path $new $f)).VersionInfo.FileVersion
        if ($v -ne $PrivateNetBuild) { Fail ".NET 1.1 SP1 was not applied: $f is version $v, not $PrivateNetBuild" }
    }
    if (Test-Path -LiteralPath $rt) { Reset-Dir $rt; Remove-Item -LiteralPath $rt -Force }
    Rename-Item -LiteralPath $new -NewName $PrivateNetVersion
    $why = Test-PrivateDotNet $rt
    if ($why) {
        Fail (".NET 1.1 was unpacked into $rt but does not run ($why). If an antivirus is active, add the install folder " +
              "and %TEMP%\NASCAR_GVR_Install as exclusions and run Install.bat again; otherwise send us %TEMP%\NASCAR_GVR_Install\install.log.")
    }
    Copy-FrameworkToShell $rt $bin
    Remove-Item -LiteralPath $w, $adm, $sp -Recurse -Force -ErrorAction SilentlyContinue
    Log "  .NET 1.1 SP1 ready: $rt"
}

# NASCAR_GVR.exe imports d3d9 directly and no d3dx9 helper, so the June-2010 redist
# is normally unnecessary. Opt in with -InstallDirectX.
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

# App-local DXVK (D3D9 -> Vulkan) beside the game. Some D3D9-era engines tie physics to
# the framerate and run absurdly fast on modern GPUs; whether NASCAR's EA engine does is
# UNVERIFIED (the cabinet ran 1360x768 on a fixed-refresh panel), so this is opt-in: use -Dxvk
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
    # nascar_settings.ini values on every start. Nothing machine-wide, no collision with other
    # GVR titles (gvr\Plus\1.1\Cabinet is shared between the titles in the real registry).
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

# The 19 race cars on the disc that have no driver select slot become playable drivers. Their
# pictures (portrait, name plate, number, leaderboard badge, menu car) are built on this PC from
# the disc's own files by Tools\GvrDriverArt.exe (the release ships no game art); it also writes
# the driver list (ExtraDriverN) into nascar_registry.ini, which the patched driver select scene
# reads. Players may replace the pictures in Shell\Textures\Faces, Names and Numbers (kept as
# player data); NascarLaunch.exe rebuilds the scene's picture sheets at start when one changed.
# Team Racing's portraits of drivers both games share are used while the portrait is the
# placeholder, when Team Racing is installed on this PC. A failure only warns: the menus then
# keep the 12 original drivers.
function Find-TeamRacingTextures {
    $cand = @()
    foreach ($k in @(Get-ChildItem $UninstallKeyViews -ErrorAction SilentlyContinue | Where-Object { $_.PSChildName -like "NASCAR_GVR_2.0.2*" })) {
        $loc = (Get-ItemProperty -Path $k.PSPath -ErrorAction SilentlyContinue).InstallLocation
        if ($loc) { $cand += $loc }
    }
    foreach ($d in [IO.DriveInfo]::GetDrives()) {
        if ($d.DriveType -eq "Fixed" -and $d.IsReady) { $cand += (Join-Path $d.RootDirectory.FullName "Games\NASCAR_GVR_2.0") }
    }
    foreach ($c in $cand) {
        $t = Join-Path ([string]$c).TrimEnd('\') "Shell\Textures"
        $exe = Join-Path $c "Game\NASCAR_GVR.exe"
        if ((Test-Path -LiteralPath $exe -PathType Leaf) -and (Get-FileSha256 $exe) -eq $Exe202Sha256 -and (Test-Path -LiteralPath $t -PathType Container)) { return $t }
    }
    return ""
}

# "Force LOD A": from a .gen (car model list), make the version that always draws the car's most detailed models.
# Only the car body (Instance=SLOT...) and the parts that come off in a crash (Instance=DEBRIS...) - tyres,
# driver, helmet, engine, flaps and effects keep their own detail steps, and shadows keep their distances.
# In every quality section (<LOW> <MED> <HIGH> <MAX>, also with a tag in front such as <NOTSPIN><LOW>) a part
# is a chain of MeshFile lines (LODIn 0 -> ... -> LODOut). Each chain becomes ONE line: the first model of the
# same chain in the <MAX> section with the same tag (the "A" model), from 0 to that chain's last distance;
# the chain's other lines are commented out with //.
function New-LodAGen([string]$src, [string]$dst) {
    $lines = [IO.File]::ReadAllLines($src)
    $rx = '^(?<tag>(<[A-Z]+>)*?)(?<sec><(LOW|MED|HIGH|MAX)>)(?<pre>\s+MeshFile=)(?<mesh>\S+)(?<mid>.*?LODIn=\()(?<in>[\d.]+)(?<mid2>\)\s+LODOut=\()(?<out>[\d.]+)(?<tail>\).*)$'
    $inst = @(); $cur = $null                                     # per instance: "tag|section" -> chains (line indices)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*(<[A-Z]+>)*\s*Instance=(?<n>[A-Za-z]+)') {
            $cur = $null
            if ($Matches.n -like 'SLOT*' -or $Matches.n -like 'DEBRIS*') { $cur = @{}; $inst += , $cur }
            continue
        }
        if ($null -eq $cur -or $lines[$i] -match 'ShadowObject=True' -or $lines[$i] -notmatch $rx) { continue }
        $key = $Matches.tag + '|' + $Matches.sec
        if (!$cur.ContainsKey($key)) { $cur[$key] = New-Object System.Collections.ArrayList }
        $chains = $cur[$key]
        if ([double]$Matches['in'] -eq 0 -or $chains.Count -eq 0) { [void]$chains.Add((New-Object System.Collections.ArrayList)) }
        [void]$chains[$chains.Count - 1].Add($i)
    }
    $out = [string[]]$lines.Clone(); $changed = 0
    foreach ($c in $inst) {
        foreach ($key in @($c.Keys)) {
            $maxKey = $key.Split('|')[0] + '|<MAX>'
            if (!$c.ContainsKey($maxKey)) { continue }
            $max = $c[$maxKey]; $chains = $c[$key]
            if ($chains.Count -ne $max.Count) { continue }          # sections not built alike: leave this one alone
            for ($k = 0; $k -lt $chains.Count; $k++) {
                [void]($lines[$max[$k][0]] -match $rx); $mesh = $Matches.mesh
                [void]($lines[$max[$k][$max[$k].Count - 1]] -match $rx); $far = $Matches.out
                $first = $chains[$k][0]
                [void]($lines[$first] -match $rx)
                $new = $Matches.tag + $Matches.sec + $Matches.pre + $mesh + $Matches.mid + '0.0' + $Matches.mid2 + $far + $Matches.tail
                if ($new -ne $lines[$first]) { $out[$first] = $new; $changed++ }
                for ($j = 1; $j -lt $chains[$k].Count; $j++) { $out[$chains[$k][$j]] = '//' + $lines[$chains[$k][$j]]; $changed++ }
            }
        }
    }
    [IO.File]::WriteAllLines($dst, $out, [Text.Encoding]::ASCII)
    return $changed
}

# [Display] MaxCarDetail (NASCAR Racing): keeps the disc's car model lists as <name>.gen.oem and makes the
# max-detail versions (New-LodAGen) as <name>.gen.loda; NascarLaunch puts the chosen one in place at every
# start. Made on the player's PC from the disc's own files - nothing modified is shipped.
function Install-CarDetail($game) {
    $dir = Join-Path $game "GameData\Vehicles\2005 NEXTEL Cup"
    foreach ($g in "2005_NEXTEL_Cup.gen", "2005_NEXTEL_Cup_Fantasy.gen") {
        $cur = Join-Path $dir $g; $oem = "$cur.oem"; $loda = "$cur.loda"
        if (!(Test-Path -LiteralPath $cur)) { Warn "  car detail: $g not found - MaxCarDetail has no effect"; continue }
        if ($DryRun) { Log "  would keep $g as .oem and make its max-detail version"; continue }
        if (!(Test-Path -LiteralPath $oem)) {
            # a list already edited by hand to max detail would become the "original": then leave it alone
            $probe = Join-Path $LogDir "cardetail-probe.gen"
            if ((New-LodAGen $cur $probe) -eq 0) { Warn "  car detail: $g is already a max-detail list (edited by hand) - left as it is"; continue }
            Copy-Item -LiteralPath $cur -Destination $oem
        }
        $n = New-LodAGen $oem $loda
        Log "  car detail: $g kept as .oem, max-detail version made ($n lines)"
    }
}
# Extra game files that come with the release (Deploy\GameFiles, laid out like the Game folder), e.g.
# per-car rims: the race reads <paint folder>\<paint name>rims.bmp after the shared wcrims.bmp (seen
# 2026-11-14: Dodge\2\2-01\2-01rims.bmp). Copied on every install and update; files of the disc are
# never replaced (only names the disc does not have, or a file this stage copied before).
function Install-GameFiles($game) {
    $src = Join-Path $SourceRoot "Deploy\GameFiles"
    if (!(Test-Path -LiteralPath $src)) { return }
    $files = @(Get-ChildItem -LiteralPath $src -Recurse -File)
    if ($DryRun) { Log "would copy $($files.Count) extra game file(s) into $game"; return }
    $list = Join-Path $game "gvr-gamefiles.txt"                       # what this stage copied before
    $ours = @{}; if (Test-Path -LiteralPath $list) { foreach ($l in Get-Content -LiteralPath $list) { if ($l) { $ours[$l.ToLowerInvariant()] = $true } } }
    $n = 0; $skip = 0; $done = @()
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($src.Length + 1)
        $dst = Join-Path $game $rel
        if ((Test-Path -LiteralPath $dst) -and !$ours.ContainsKey($rel.ToLowerInvariant())) { Warn "  extra game file not copied - the game already has $rel"; $skip++; continue }
        New-Dir (Split-Path -Parent $dst)
        Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
        $done += $rel; $n++
    }
    Set-Content -LiteralPath $list -Value $done -Encoding ASCII
    Log "--- extra game files: $n copied$(if ($skip) { ", $skip skipped" }) ---"
}

function Install-ExtraDrivers($root, $shell) {
    if ($SkipShell) { return }
    Log "--- extra drivers (pictures built from this install's disc files) ---"
    $src = Join-Path $SourceRoot "src\DriverArt\GvrDriverArt.exe"
    $data = Join-Path $SourceRoot "Patches\extra-drivers-11.txt"
    $tools = Join-Path $root "Tools"
    if ($DryRun) { Log "  would copy GvrDriverArt.exe + extra-drivers.txt to $tools and build the pictures"; return }
    if (!(Test-Path -LiteralPath $src) -or !(Test-Path -LiteralPath $data)) { Warn "extra drivers skipped: GvrDriverArt.exe or extra-drivers-11.txt missing from the package"; return }
    New-Dir $tools
    Copy-Item -LiteralPath $src -Destination (Join-Path $tools "GvrDriverArt.exe") -Force
    Copy-Item -LiteralPath $data -Destination (Join-Path $tools "extra-drivers.txt") -Force
    # pictures that come with this release (Deploy\DriverArt\Faces, Names, Numbers): the builder puts one in
    # place of a player's file only while that file is still the automatic default or the previous release's
    # picture, so a player's own pictures always stay. Community is ours: refreshed on every run.
    $comm = Join-Path $SourceRoot "Deploy\DriverArt"
    $commDst = Join-Path $shell "Textures\Community"
    if (Test-Path -LiteralPath $comm) {
        if (Test-Path -LiteralPath $commDst) { Remove-Item -LiteralPath $commDst -Recurse -Force -ErrorAction SilentlyContinue }
        New-Dir $commDst
        Copy-Item -Path (Join-Path $comm "*") -Destination $commDst -Recurse -Force
        Log ("  community pictures: " + @(Get-ChildItem -LiteralPath $commDst -Recurse -File).Count + " file(s) in Shell\Textures\Community")
    }
    $tr = Find-TeamRacingTextures
    if ($tr) { Log "  Team Racing found: its portraits of shared drivers are used ($tr)" }
    # PowerShell 5.1 turns a native program's stderr line into a terminating error under "Stop";
    # this stage must only warn (e.g. no .NET 4 on the PC)
    $eap = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    try { $out = & (Join-Path $tools "GvrDriverArt.exe") install $root $tr 2>&1; $code = $LASTEXITCODE }
    catch { $out = @($_.Exception.Message); $code = 2 }
    finally { $ErrorActionPreference = $eap }
    # the launcher runs the builder at every start: without a working builder (exit 2) it is removed,
    # so the start is not delayed by a program that cannot run here
    if ($code -ge 2) { Remove-Item -LiteralPath (Join-Path $tools "GvrDriverArt.exe") -Force -ErrorAction SilentlyContinue }
    foreach ($l in @($out)) { Log "  $l" }
    if ($code -eq 0) { Log "  extra drivers ready" }
    elseif ($code -eq 1) { Warn "some extra drivers could not be built (listed above) - the others are playable" }
    else { Warn "extra drivers could not be built (exit $code) - the menus keep the 12 original drivers" }
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
    $icoSrc = Join-Path $srcDir $IconFile

    if (!(Test-Path $exeSrc)) {
        # It IS in every release zip (and needs no Visual Studio to run); Assert-ReleaseFiles stops a
        # release install without it. No plain-shell fallback any more: without the launcher the game
        # runs without its settings or private-registry preload and looks broken. A developer
        # checkout must build it (src\NascarLaunch\build.cmd).
        Fail "NascarLaunch.exe is missing: $exeSrc - unzip the release again (antivirus?) and re-run Install.bat."
    }
    Copy-FileIfPresent $exeSrc (Join-Path $root "NascarLaunch.exe") "NascarLaunch.exe"
    Copy-FileIfPresent $icoSrc (Join-Path $root $IconFile) "icon"

    # The settings file is the player's; never overwrite an edited one.
    $iniDst = Join-Path $root "nascar_settings.ini"
    if ((Test-Path $iniDst) -and !$ResetData) { Log "  keeping your existing nascar_settings.ini"; Update-OldDefaults $iniDst; Rebuild-Settings $iniDst $iniSrc }
    else {
        Copy-FileIfPresent $iniSrc $iniDst "nascar_settings.ini"
        # seed it with the values chosen on the command line
        if (!$DryRun -and (Test-Path $iniDst)) {
            $t = [IO.File]::ReadAllText($iniDst)
            $t = $t -replace '(?m)^Width\s*=.*$',  "Width  = $Width"
            $t = $t -replace '(?m)^Height\s*=.*$', "Height = $Height"
            $t = $t -replace '(?m)^Fullscreen\s*=.*$', ("Fullscreen = " + $(if ($Fullscreen) { "true" } else { "false" }))
            # Default to a borderless window that fills the screen (the race is windowed otherwise
            # and, at a size smaller than the desktop, shows as a small window). -Fullscreen opts
            # into exclusive fullscreen instead, so Borderless must be off for it to take effect.
            $t = $t -replace '(?m)^Borderless\s*=.*$', ("Borderless = " + $(if ($Fullscreen) { "false" } else { "true" }))
            $t = $t -replace '(?m)^Track\s*=.*$',  "Track = $Track"
            $t = $t -replace '(?m)^Series\s*=.*$', "Series = $Series"
            if ($SkipShell) { $t = $t -replace '(?m)^Mode\s*=\s*shell\s*$', "Mode = race" }
            [IO.File]::WriteAllText($iniDst, $t)
            Log ("  nascar_settings.ini seeded (" + $(if ($Width -le 0) { "screen size" } else { "$Width x $Height" }) + ", " + $(if ($Fullscreen) { "fullscreen" } else { "borderless" }) + ")")
        }
    }

    if (!$NoShortcut -and !$DryRun) {
        $target = Join-Path $root "NascarLaunch.exe"
        $useLauncher = Test-Path $target
        # never take over a shortcut that starts ANOTHER install (another copy, a future edition)
        $lnkName = $ShortcutName
        $lnk = Join-Path ([Environment]::GetFolderPath("Desktop")) "$lnkName.lnk"
        if (!(Test-OurShortcut $lnk $root)) {
            $lnkName = "$ShortcutName (" + (Split-Path -Leaf $root) + ")"
            $lnk = Join-Path ([Environment]::GetFolderPath("Desktop")) "$lnkName.lnk"
            Log "  a '$ShortcutName' shortcut already starts another install - this one is '$lnkName'"
        }
        $ws = New-Object -ComObject WScript.Shell
        $s = $ws.CreateShortcut($lnk)
        if ($useLauncher) {
            $s.TargetPath = $target
            $s.IconLocation = (Join-Path $root $IconFile) + ",0"
        }
        else {
            $s.TargetPath = Join-Path $shell "bin\AMPlayer.exe"
            $s.Arguments = "`"" + (Join-Path $shell "Shell.am") + "`" -v FULL"
            $s.IconLocation = (Join-Path $root $IconFile) + ",0"
        }
        $s.WorkingDirectory = $root
        $s.Description = "$ShortcutName (GlobalVR)"
        $s.Save()
        Log "  desktop shortcut -> $($s.TargetPath)"

        # Migrate the pre-4.15 "NASCAR Team Racing" shortcut of THIS install (the name the 1.1
        # edition used before the 1.1/2.0.2 split) so the player is not left with two icons. Only
        # a shortcut that points into THIS install is touched - a real 2.0.2 "NASCAR Team Racing"
        # shortcut points elsewhere and is left alone.
        if ($LegacyShortcutName -ne $ShortcutName) {
            foreach ($d in @([Environment]::GetFolderPath("Desktop"), [Environment]::GetFolderPath("Programs"))) {
                $old = Join-Path $d "$LegacyShortcutName.lnk"
                if ((Test-Path $old) -and (Test-OurShortcut $old $root)) {
                    Remove-Item -LiteralPath $old -Force -ErrorAction SilentlyContinue
                    Log "  migrated old '$LegacyShortcutName' shortcut to '$ShortcutName'"
                }
            }
        }

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
        if (!$SkipDotNet) {
            Need (Join-Path $root "Runtime\$PrivateNetVersion\mscorwks.dll") $true "private .NET 1.1 SP1 the menus run on"
            Need (Join-Path $shell "bin\System.Data.dll")    $true  ".NET library next to AMPlayer.exe (no global assembly cache)"
        }
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

# The launcher's [Display] Gpu setting stores a Windows graphics-chip choice per program
# (HKCU\Software\Microsoft\DirectX\UserGpuPreferences, value name = full path of the exe). Remove the ones inside $root.
function Remove-GpuChoices($root) {
    $key = "HKCU:\Software\Microsoft\DirectX\UserGpuPreferences"
    if (!(Test-Path $key)) { return }
    $prefix = ($root.TrimEnd('\') + '\').ToLowerInvariant()
    foreach ($p in @((Get-ItemProperty -Path $key).PSObject.Properties | Where-Object { $_.Name.ToLowerInvariant().StartsWith($prefix) })) {
        if ($DryRun) { Log "  would remove the graphics-chip choice for $($p.Name)"; continue }
        Remove-ItemProperty -Path $key -Name $p.Name -ErrorAction SilentlyContinue
        Log "  removed the graphics-chip choice for $($p.Name)"
    }
}

function Invoke-Uninstall($root) {
    Log "=== UNINSTALL ==="
    $kind = Get-InstallKind $root
    if (!(Test-OurKind $kind)) { Fail ("Not removing $root - it holds " + (Get-KindText $kind) + ". Nothing was removed.") }
    $nl = [Environment]::NewLine
    $keep = Join-Path ([Environment]::GetFolderPath("MyDocuments")) ("NASCAR_GVR Backup\" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    if ($ForceOverwrite -and !$DryRun) {
        $ok = Ask-YesNo ("Remove $ShortcutName from this PC?" + $nl + $nl + $root + $nl + $nl +
                         "Your scores, settings and calibration are copied to Documents\NASCAR_GVR Backup first.")
        if ($ok -eq $false) { Log "uninstall cancelled - nothing was removed"; exit 0 }
        Set-Location -LiteralPath $env:TEMP          # never hold the folder that is about to go
    }
    if (Test-Path $root) {
        if (!$ForceOverwrite) { Warn "would remove $root  (re-run with -ForceOverwrite to delete)" }
        elseif ($DryRun) { Log "would remove $root" }
        else {
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
        foreach ($l in @(Get-ChildItem -LiteralPath $dir -Filter "NASCAR*Racing*.lnk" -ErrorAction SilentlyContinue)) {
            # only shortcuts that start THIS install
            if ((Test-OurShortcut $l.FullName $root) -and !$DryRun) { Remove-Item -LiteralPath $l.FullName -Force; Log "  removed shortcut $($l.Name)" }
        }
    }
    if ($ForceOverwrite) { Remove-GpuChoices $root; Unregister-Uninstall $root }
    Log "uninstall pass complete"
    if ($ForceOverwrite -and !$DryRun) { Show-Message ("$ShortcutName was removed." + $nl + $nl + "Your scores, settings and calibration were copied to:" + $nl + $keep) }
}

# ============================ run =========================================

Log "============================================================"
Log "NASCAR GlobalVR portable installer $Version"
Log "============================================================"

# A dry run touches nothing, so it is allowed unelevated - that is the whole point
# of being able to preview an install before committing to it.
# Started without administrator rights - Programs and Features runs Uninstall that way, and a
# player may run the script by hand: ask Windows for them and run again with the same options.
if (!(Test-Admin) -and !$DryRun) {
    $relaunch = @("-NoProfile", "-STA", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"")
    foreach ($p in $PSBoundParameters.GetEnumerator()) {
        if ($p.Value -is [System.Management.Automation.SwitchParameter]) { if ($p.Value.IsPresent) { $relaunch += "-$($p.Key)" } }
        else { $relaunch += "-$($p.Key)"; $relaunch += "`"$($p.Value)`"" }
    }
    Log "not elevated - asking Windows for administrator rights"
    try { Start-Process -FilePath (Join-Path $PSHOME "powershell.exe") -Verb RunAs -ArgumentList $relaunch | Out-Null; exit 0 }
    catch { Fail "Administrator rights are needed to install or remove the game (Windows said: $($_.Exception.Message))." }
}
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
    "Install.bat", "Install-NASCAR-GVR-Portable.ps1", "Install-NASCAR.ps1",
    "Patches\nascar-bytepatches.txt", "Patches\extra-drivers-11.txt", "src\DriverArt\GvrDriverArt.exe",
    "src\GvrLinking\build\GvrLinking.dll",
    "Deploy\PLUSDE.dll", "Deploy\GvrSqlite.dll", "Deploy\sqlite3.dll", "Deploy\game.db",
    "Tools\unshield.exe", "Tools\Fix-GvrDirNames.ps1", "Tools\Check-DotNet11.ps1",
    "src\GvrIOShim\build\GvrIO.dll",
    "src\NascarLaunch\build\NascarLaunch.exe", "src\NascarLaunch\nascar_settings.ini", "src\NascarLaunch\NASCAR_Racing.ico",
    "src\GvrSqlite\GvrSqlite.cs",
    "Dependencies\DotNet11\dotnetfx.exe", "Dependencies\DotNet11\NDP1.1sp1-KB867460-X86.exe",
    "Dependencies\DXVK\d3d9.dll"
)
function Assert-ReleaseFiles {
    if ($Uninstall) { return }                      # removing an install must work from any folder
    # A release package (and the NASCAR_GIT repo) always has Install.bat - it is what users run.
    # A development folder does not, and finds several of these elsewhere.
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

# An install of this game is already on the PC: offer to update it (the usual case for players).
if ([string]::IsNullOrEmpty($InstallRoot) -and !$Uninstall) {
    foreach ($e in @(Find-ExistingInstalls)) {
        Log "existing install found: $e"
        $nl = [Environment]::NewLine
        $q = "$ShortcutName is already installed in:" + $nl + $e + $nl + $nl +
             "Update it now? No game disc is needed; your scores, settings and calibration are kept." + $nl + $nl +
             "Yes = update this install        No = choose another folder"
        $a = Ask-YesNo $q
        if ($null -eq $a) {
            Write-Host ""; Write-Host "$ShortcutName is already installed in $e" -ForegroundColor Cyan
            $a = (Read-Host "Update it (no disc needed, scores and settings kept)? [Y/n]") -notmatch '^[nN]'
        }
        if ($a) { $InstallRoot = $e; break }
    }
}

if ([string]::IsNullOrEmpty($InstallRoot) -and (Test-CanShowDialogs)) {
    while ($true) {
        $pick = Select-Folder "Where should NASCAR Racing be installed?`r`nPick a folder or drive (for example C:\Games) - a $FolderLeaf folder is created inside it." ""
        if (!$pick) { Log "cancelled - nothing was installed"; exit 0 }
        $pick = $pick.TrimEnd("\")
        if ($pick -match '^[A-Za-z]:$') { $InstallRoot = "$pick\$FolderLeaf" }
        # an existing install of OURS is used as it is (to update it); any other folder - another
        # NASCAR game, another GVR title, the 2.0.2 edition - gets a NASCAR_GVR folder inside it
        elseif (@("ours", "ours-legacy", "ours-partial") -contains (Get-InstallKind $pick)) { $InstallRoot = $pick }
        else { $InstallRoot = Join-Path $pick $FolderLeaf }
        $k = Get-InstallKind $InstallRoot
        if (!(Test-OurKind $k)) {
            Show-Message ("This folder already holds " + (Get-KindText $k) + ":`r`n$InstallRoot`r`n`r`nNothing in it is touched. Pick another folder, for example C:\Games.") "Warning"
            $InstallRoot = $null
            continue
        }
        $why = Get-UnsafeFolderReason $InstallRoot
        if ($why -and $k -ne "ours" -and $k -ne "ours-legacy" -and $k -ne "ours-partial") {
            Show-Message ("The game cannot be installed here:`r`n$InstallRoot`r`n`r`nReason: $why.`r`n`r`nPick a simple folder, for example C:\Games.") "Warning"
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
    Write-Host "  Examples:  C:\Games\$FolderLeaf     D:\Games\$FolderLeaf     E:\Arcade\$FolderLeaf"
    Write-Host ""
    $def = "C:\Games\$FolderLeaf"   # not D:\ - that is often the DVD drive holding the game disc
    $InstallRoot = Read-Host "Install folder [$def]"
    if ([string]::IsNullOrEmpty($InstallRoot)) { $InstallRoot = $def }
}
$InstallRoot = $InstallRoot.TrimEnd("\")
if ($InstallRoot -match '^[A-Za-z]:$' -or $InstallRoot -match '^[A-Za-z]:\\$') { Fail "Refusing to install to a drive root: $InstallRoot" }
if (!$Uninstall) {
    $why = Get-UnsafeFolderReason $InstallRoot
    if ($why) {
        if (@("ours", "ours-legacy", "ours-partial") -contains (Get-InstallKind $InstallRoot)) {
            Warn "this install is in a folder the game may not work from: $why. If it does not start, install again into a simple folder such as C:\Games."
            if (!$NoGui) { Show-Message ("This install is in a folder the game may not work from:`r`n$InstallRoot`r`n`r`nReason: $why.`r`n`r`nThe update continues. If the game does not start, install it again into a simple folder such as C:\Games.") "Warning" }
        } else { Fail "Not installing into $InstallRoot - $why. Choose a simple folder, for example C:\Games." }
    }
}
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
            $pick = Select-Folder "Select the NASCAR Racing v1.1 game disc: the drive of the mounted ISO or disc (or a folder holding data1.cab)." "" -FromComputer
            if (!$pick) { Log "cancelled - nothing was installed"; exit 0 }
            $cand = $pick.TrimEnd("\"); if ($cand -match '^[A-Za-z]:$') { $cand += "\" }
            if (Test-Path (Join-Path $cand "data1.cab")) { break }
            Show-Message "data1.cab was not found in:`r`n$pick`r`n`r`nMount the NASCAR ISO (right-click it, Mount) and pick its drive." "Warning"
        }
    }
    else {
        while ($true) {
            Write-Host ""
            Write-Host "Insert or mount the NASCAR Racing v1.1 game disc (the one with data1.cab)." -ForegroundColor Cyan
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

if (!$already -or $ForceOverwrite) {
    Install-Payload $Game $Shell $Gvr
    if ($PlayerBackup -and !$ResetData) { Restore-RaceSaves $PlayerBackup $Game }
}
Fix-DirNames $Game
Install-SupportDlls $Game $Shell
Install-GvrIoShim $Game $Shell
Install-GvrLinking $Shell
Install-DDrawCompat $Shell
Install-GvrGlu $Shell
Install-MenuTextures $InstallRoot
Disable-CabinetPlugIns $Game
Repoint-ShellPaths $Shell
Apply-BytePatches $InstallRoot
Edit-OperatorXml $Shell
Edit-GvrIoXml $Game
Disable-GammaSet $Shell
Install-ClrPin $Shell
Install-PrivateDotNet $InstallRoot $Shell
Install-Sqlite $Game $Shell $Plus $Db
Ensure-DirectX
Deploy-Dxvk $Game
Install-Fonts $InstallRoot
Install-Registry $InstallRoot
Install-GameFiles $Game
Install-CarDetail $Game
Install-ExtraDrivers $InstallRoot $Shell
Install-OemRev
Install-Launcher $InstallRoot $Game $Shell
Verify-Deployment $InstallRoot $Game $Shell $Plus $Db
Write-InstallMarker $InstallRoot
Register-Uninstall $InstallRoot

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
    $msg = "NASCAR Racing is " + $(if ($UpdateMode) { "updated" } else { "installed" }) + " in:" + $nl + $InstallRoot + $nl + $nl + "Start it with the $ShortcutName shortcut on your desktop." + $nl + "Settings: nascar_settings.ini in that folder."
    Show-Message $msg
}
