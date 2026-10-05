<#
.SYNOPSIS
    Restores the directory names that unshield mangled while extracting the
    NASCAR OEM cabinet.

.DESCRIPTION
    unshield's default extraction converts characters in DIRECTORY names -
    spaces become underscores - while leaving FILE names alone. (The `-R`
    switch turns that conversion off; Extract-NASCAR-Sources.ps1 now passes it,
    so freshly extracted payloads are already correct.)

    That rename is not cosmetic. NASCAR_GVR.exe holds the affected directory
    names as string literals and enumerates them at startup:

        DAT_0078e6e8+0x1C0  "CreateACar\"
        DAT_0078e6e8+0x1D0  "__CreateACar\"
        DAT_0078e6e8+0x1E0  "Basic Settings\"     <-- searched for "*.CTL"
        DAT_0078e6e8+0x1F0  "UI Elements\"

    At 0x0048B941 the game enumerates "<dir>*.CTL"; at 0x0048B95D it takes the
    first match. When the search finds nothing it stores NULL as the current
    entry (0x0048B97D), calls the path resolver anyway (0x004F9A70, which
    returns NULL because entry+8 is 0) and then strcpy()s from that NULL at
    0x0048B990 - an access violation inside Game::CreateManagers. The game
    installs an unhandled-exception filter that logs to a FILE* the release
    build never opens and then _exit(-1)s, so the process just vanishes with no
    log and no WER event. So a single mangled directory name is a silent,
    invisible startup crash.

    The 7 affected names are taken from the cabinet's own directory table
    (`unshield l data1.cab`), not guessed: every OTHER underscore in the tree
    (END_OF_SEASON, MAIN_MENU, PP_SOT, Career_NEXTEL...) is genuine, so this
    must never be a blanket underscore-to-space replacement.

.PARAMETER Root
    Tree to repair - either a deployed install (D:\Games\NASCAR) or an
    extracted payload (...\Extracted\Disc\File_Group\NASCAR).

.EXAMPLE
    .\Fix-GvrDirNames.ps1 -Root "D:\Games\NASCAR"
    .\Fix-GvrDirNames.ps1 -Root "D:\Games\NASCAR" -DryRun
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
trap { Write-Host "FAILED: $_" -ForegroundColor Red; exit 1 }

# Correct names, straight out of the cabinet directory table.
$CorrectNames = @(
    "2004 NNS",
    "2005 NEXTEL Cup",
    "Basic Settings",
    "New Hampshire",
    "Pit Elements",
    "Tab Backgrounds",
    "UI Elements"
)

function Log ($m) { Write-Host $m }

if (-not (Test-Path -LiteralPath $Root)) { throw "Root not found: $Root" }
$Root = (Resolve-Path -LiteralPath $Root).Path

# mangled form -> correct form
$map = @{}
foreach ($n in $CorrectNames) { $map[$n.Replace(" ", "_")] = $n }

Log "Fix-GvrDirNames: $Root"
if ($DryRun) { Log "  (dry run - nothing will be renamed)" }

# Deepest-first, so renaming a child never invalidates a parent path we still
# have queued. Each rename only touches the leaf component.
$all = Get-ChildItem -LiteralPath $Root -Recurse -Directory -Force -ErrorAction SilentlyContinue |
       Sort-Object { $_.FullName.Length } -Descending

$renamed = 0; $already = 0; $clash = 0
foreach ($d in $all) {
    $want = $map[$d.Name]
    if (-not $want) { continue }

    $target = Join-Path $d.Parent.FullName $want
    # Windows is case/space-insensitive about nothing here: "A_B" and "A B" are
    # genuinely different names, so an existing target means a real collision.
    if (Test-Path -LiteralPath $target) {
        Log "  CLASH  both forms exist, leaving alone: $($d.FullName)"
        $clash++
        continue
    }

    $rel = $d.FullName.Substring($Root.Length).TrimStart('\')
    Log "  rename $rel  ->  $want"
    if (-not $DryRun) { Rename-Item -LiteralPath $d.FullName -NewName $want -Force }
    $renamed++
}

# Count the ones that were already right, for an honest summary.
foreach ($n in $CorrectNames) {
    $already += (Get-ChildItem -LiteralPath $Root -Recurse -Directory -Force -Filter $n -ErrorAction SilentlyContinue |
                 Measure-Object).Count
}

Log ""
Log "  renamed        : $renamed"
Log "  already correct: $already"
if ($clash) { Log "  collisions     : $clash  (inspect manually)" }
if ($renamed -eq 0 -and $already -eq 0) {
    Log "  NOTE: none of the 7 names were found at all - is '$Root' the right tree?"
}
