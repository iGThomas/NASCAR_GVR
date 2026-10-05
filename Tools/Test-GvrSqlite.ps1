# Builds and runs the GvrSqlite functional test against a NASCAR game.db.
# Uses the real .NET 1.1 compiler when present (that is what the game runs on),
# otherwise falls back to the 2.0 compiler with /platform:x86 -- sqlite3.dll is
# 32-bit, so the harness must be too.

param(
    [string]$Db = "",
    [string]$Provider = "",
    [string]$Sqlite3 = "D:\NFSU_GVR\GvrSqlite\deploy\sqlite3.dll",
    [switch]$KeepTempDir
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$nascar = Split-Path -Parent $here
if ([string]::IsNullOrEmpty($Db)) { $Db = Join-Path $nascar "Extracted\Schema\game.db" }
if ([string]::IsNullOrEmpty($Provider)) { $Provider = Join-Path $nascar "Build\GvrSqlite.dll" }

foreach ($p in @($Db, $Provider, $Sqlite3)) {
    if (!(Test-Path $p)) { throw "missing: $p" }
}

$csc11 = "C:\Windows\Microsoft.NET\Framework\v1.1.4322\csc.exe"
$csc20 = "C:\Windows\Microsoft.NET\Framework\v2.0.50727\csc.exe"
$csc = $null; $flags = @()
if (Test-Path $csc11) { $csc = $csc11 }
elseif (Test-Path $csc20) { $csc = $csc20; $flags = @("/platform:x86") }
else { throw "no usable csc.exe found" }

$tmp = Join-Path $env:TEMP ("gvrsqlite_test_" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $tmp -Force | Out-Null
try {
    Copy-Item $Provider (Join-Path $tmp "GvrSqlite.dll") -Force
    Copy-Item $Sqlite3 (Join-Path $tmp "sqlite3.dll") -Force
    Copy-Item $Db (Join-Path $tmp "game.db") -Force      # work on a copy; the test writes
    Copy-Item (Join-Path $here "TestGvrSqlite.cs") $tmp -Force

    Write-Host "compiler: $csc"
    Push-Location $tmp
    try {
        & $csc /nologo /target:exe /out:test.exe /r:GvrSqlite.dll /r:System.Data.dll @flags TestGvrSqlite.cs 2>&1 |
            Where-Object { $_ -notmatch '^\s*$' } | ForEach-Object { "  $_" }
        if ($LASTEXITCODE -ne 0) { throw "compile failed ($LASTEXITCODE)" }
        Write-Host ""
        # Point the provider at the test copy. GVRSQLITE_DB may already be set
        # machine-wide by an NFSU install; the per-title variable wins over it,
        # which is exactly the coexistence behaviour we want to test.
        $env:GVRSQLITE_DB_NAS1 = (Join-Path $tmp "game.db")
        & (Join-Path $tmp "test.exe") (Join-Path $tmp "game.db")
        $rc = $LASTEXITCODE
        Remove-Item Env:\GVRSQLITE_DB_NAS1 -ErrorAction SilentlyContinue
    }
    finally { Pop-Location }
    exit $rc
}
finally {
    if ($KeepTempDir) { Write-Host "temp dir kept: $tmp" }
    else { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
}
