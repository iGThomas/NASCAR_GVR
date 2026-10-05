# ============================================================
# Build-SqliteBackend.ps1
#
# Produces the complete SQLite backend for NASCAR in NASCAR\Deploy:
#
#   1. decrypt the OEM GvrPlus schema (.enc -> .txt)
#   2. build game.db from that schema + seed content + operator .tbl files
#   3. compile the GvrSqlite provider (.NET 1.1)
#   4. patch PLUSDE.dll's SqlClient typerefs -> GvrSqlite
#   5. stage sqlite3.dll and everything above into Deploy\
#   6. run the functional test against the built database
#
# With this in place the cabinet stack needs no MSDE / SQL Server at all.
#
#   .\Build-SqliteBackend.ps1
#   .\Build-SqliteBackend.ps1 -SkipTest
# ============================================================

param(
    [string]$Sqlite3 = "",
    [switch]$SkipTest,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$nascar = Split-Path -Parent $here
$root = Split-Path -Parent $nascar

$payload = Join-Path $nascar "Extracted\Disc\File_Group"
$schemaSrc = Join-Path $payload "GVRPLUS\4\schema"
$schemaOut = Join-Path $nascar "Extracted\Schema"
$build = Join-Path $nascar "Build"
$deploy = Join-Path $nascar "Deploy"
$providerSrc = Join-Path $root "GvrSqlite\GvrSqlite.cs"
if ([string]::IsNullOrEmpty($Sqlite3)) { $Sqlite3 = Join-Path $root "GvrSqlite\deploy\sqlite3.dll" }

function Log($m) { Write-Host ("[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $m) }
function Fail($m) { Write-Host "[X] $m" -ForegroundColor Red; throw $m }

foreach ($p in @($schemaSrc, $providerSrc, $Sqlite3)) {
    if (!(Test-Path $p)) { Fail "missing input: $p" }
}
foreach ($d in @($schemaOut, $build, $deploy)) {
    if (!(Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

# ---- 1. decrypt schema -----------------------------------------------------

$cab = Join-Path $schemaOut "NASCARcabinet.txt"
if ((Test-Path $cab) -and !$Force) {
    Log "schema already decrypted (use -Force to redo)"
}
else {
    Log "decrypting GvrPlus schema..."
    & (Join-Path $here "Decrypt-GvrEnc.ps1") $schemaSrc $schemaOut | ForEach-Object { "    $_" }
}
if (!(Test-Path $cab)) { Fail "decryption produced no NASCARcabinet.txt" }

# ---- 2. build game.db ------------------------------------------------------

Log "building game.db..."
$py = (Get-Command python -ErrorAction SilentlyContinue)
if ($null -eq $py) { Fail "python not found in PATH (needed to build the database)" }
$db = Join-Path $build "game.db"
& python (Join-Path $here "Build-NascarSqliteDb.py") `
    $cab $db (Join-Path $schemaOut "NASCARcabinet_Content.txt") (Join-Path $schemaSrc "game") |
    ForEach-Object { "    $_" }
if (!(Test-Path $db)) { Fail "game.db was not produced" }

# ---- 3. compile the provider ----------------------------------------------

$csc11 = "C:\Windows\Microsoft.NET\Framework\v1.1.4322\csc.exe"
$csc20 = "C:\Windows\Microsoft.NET\Framework\v2.0.50727\csc.exe"
$provider = Join-Path $build "GvrSqlite.dll"
if (Test-Path $csc11) {
    Log "compiling GvrSqlite with the .NET 1.1 compiler (matches the game's runtime)"
    & $csc11 /nologo /target:library /out:$provider $providerSrc 2>&1 |
        Where-Object { $_ -notmatch '^\s*$' -and $_ -notmatch 'warning CS0649' } | ForEach-Object { "    $_" }
}
elseif (Test-Path $csc20) {
    # Fallback: build with 2.0 and retarget the metadata to v1.1.4322, the route
    # the NFSU project used on a box with no 1.1 SDK.
    Log "no 1.1 compiler; building with 2.0 + retarget to v1.1.4322"
    & $csc20 /nologo /target:library /platform:x86 /out:$provider $providerSrc 2>&1 |
        Where-Object { $_ -notmatch '^\s*$' -and $_ -notmatch 'warning CS0649' } | ForEach-Object { "    $_" }
    Add-Type -Path (Join-Path $root "Tools\lib\dnlib.net45.dll")
    $pm = [dnlib.DotNet.ModuleDefMD]::Load($provider)
    $pm.RuntimeVersion = "v1.1.4322"
    foreach ($ar in $pm.GetAssemblyRefs()) {
        if ($ar.Name.String -eq 'mscorlib' -or $ar.Name.String -eq 'System' -or $ar.Name.String -eq 'System.Data') {
            $ar.Version = New-Object Version(1, 0, 5000, 0)
        }
    }
    $tmp = "$provider.tmp"
    $pm.Write($tmp)
    $pm.Dispose()
    Move-Item $tmp $provider -Force
}
else { Fail "no csc.exe found (need .NET 1.1 or 2.0 Framework)" }
if (!(Test-Path $provider)) { Fail "provider build failed" }
Log "provider: {0:N0} bytes" -f (Get-Item $provider).Length

# ---- 4. patch PLUSDE -------------------------------------------------------

Log "patching PLUSDE.dll..."
& (Join-Path $here "Patch-PlusdeToSqlite.ps1") -Provider $provider -OutputDll (Join-Path $build "PLUSDE.dll") |
    ForEach-Object { "    $_" }
if (!(Test-Path (Join-Path $build "PLUSDE.dll"))) { Fail "PLUSDE patch produced no output" }

# ---- 5. stage the deploy set ----------------------------------------------

Log "staging Deploy\..."
Copy-Item (Join-Path $build "PLUSDE.dll") (Join-Path $deploy "PLUSDE.dll") -Force
Copy-Item $provider (Join-Path $deploy "GvrSqlite.dll") -Force
Copy-Item $Sqlite3 (Join-Path $deploy "sqlite3.dll") -Force
Copy-Item $db (Join-Path $deploy "game.db") -Force
Get-ChildItem $deploy | ForEach-Object { "    {0,-16} {1,12:N0} bytes" -f $_.Name, $_.Length }

# ---- 6. functional test ----------------------------------------------------

if (!$SkipTest) {
    Log "running functional test..."
    & (Join-Path $here "Test-GvrSqlite.ps1") -Db (Join-Path $deploy "game.db") -Provider $provider -Sqlite3 $Sqlite3
    if ($LASTEXITCODE -ne 0) { Fail "functional test FAILED" }
}

Log "SQLite backend ready in $deploy"
Log "Install it with:  Install-NASCAR-GVR-Portable.ps1"
