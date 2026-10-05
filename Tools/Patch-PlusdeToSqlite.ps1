# ============================================================
# Patch-PlusdeToSqlite.ps1
#
# Rewrites NASCAR's PLUSDE.dll so its System.Data.SqlClient
# references point at the GvrSqlite provider instead. The game
# then talks to an embedded SQLite file and no SQL Server /
# MSDE needs to exist at all.
#
# PLUSDE.dll is a MIXED-MODE C++/CLI assembly (IL + native code),
# so it must be written back with dnlib's native writer -- a
# normal managed write would drop the native half.
#
# Only metadata references change; not a single IL instruction or
# native byte is rewritten. Every swapped member is verified to
# exist on the provider first (pre-flight), and again after the
# write (verify).
#
#   .\Patch-PlusdeToSqlite.ps1                  # patch + verify
#   .\Patch-PlusdeToSqlite.ps1 -VerifyOnly      # inspect an existing patched DLL
# ============================================================

param(
    [string]$InputDll = "",
    [string]$OutputDll = "",
    [string]$Provider = "",
    [string]$DnlibPath = "",
    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$nascar = Split-Path -Parent $here
if ([string]::IsNullOrEmpty($InputDll)) {
    $InputDll = Join-Path $nascar "Extracted\Disc\File_Group\WINDOWS\system32\PLUSDE.dll"
}
if ([string]::IsNullOrEmpty($OutputDll)) { $OutputDll = Join-Path $nascar "Build\PLUSDE.dll" }
if ([string]::IsNullOrEmpty($Provider)) { $Provider = Join-Path $nascar "Build\GvrSqlite.dll" }
if ([string]::IsNullOrEmpty($DnlibPath)) { $DnlibPath = Join-Path (Split-Path -Parent $nascar) "Tools\lib\dnlib.net45.dll" }

# SqlClient type -> GvrSqlite type. DataAdapter/DbDataAdapter both collapse onto
# GvrDataAdapter because our provider is not a real ADO.NET hierarchy -- it is a
# flat mirror of just the members the game actually calls.
$MAP = @{
    'System.Data.SqlClient.SqlConnection'           = 'GvrConnection'
    'System.Data.SqlClient.SqlCommand'              = 'GvrCommand'
    'System.Data.SqlClient.SqlDataAdapter'          = 'GvrDataAdapter'
    'System.Data.SqlClient.SqlParameter'            = 'GvrParameter'
    'System.Data.SqlClient.SqlParameterCollection'  = 'GvrParameterCollection'
    'System.Data.SqlClient.SqlError'                = 'GvrError'
    'System.Data.SqlClient.SqlErrorCollection'      = 'GvrErrorCollection'
    'System.Data.SqlClient.SqlException'            = 'GvrException'
    'System.Data.SqlClient.SqlInfoMessageEventArgs' = 'GvrInfoMessageEventArgs'
    'System.Data.Common.DbDataAdapter'              = 'GvrDataAdapter'
    'System.Data.Common.DataAdapter'                = 'GvrDataAdapter'
}
$PROVIDER_NS = 'GvrSqlite'

function Log($m) { Write-Host ("[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $m) }
function Fail($m) { Write-Host "[X] $m" -ForegroundColor Red; throw $m }

if (!(Test-Path $DnlibPath)) { Fail "dnlib (net45 build) not found at $DnlibPath" }
Add-Type -Path $DnlibPath
if (!(Test-Path $Provider)) { Fail "Provider assembly not found: $Provider (build it with Build-GvrSqlite.ps1)" }

# ---- provider surface ------------------------------------------------------

$prov = [dnlib.DotNet.ModuleDefMD]::Load($Provider)
$provTypes = @{}
foreach ($t in $prov.GetTypes()) { $provTypes[$t.Name.String] = $t }

function Test-ProviderMember($typeName, $memberName) {
    $pt = $provTypes[$typeName]
    if ($null -eq $pt) { return $false }
    foreach ($mm in $pt.Methods) { if ($mm.Name.String -eq $memberName) { return $true } }
    foreach ($f in $pt.Fields) { if ($f.Name.String -eq $memberName) { return $true } }
    foreach ($pp in $pt.Properties) { if ($pp.Name.String -eq $memberName) { return $true } }
    # inherited members (e.g. Exception.Message on GvrException) resolve at runtime
    if ($pt.BaseType -and $pt.BaseType.Name.String -eq 'Exception') { return $true }
    return $false
}

# ---- load target -----------------------------------------------------------

$src = if ($VerifyOnly) { $OutputDll } else { $InputDll }
if (!(Test-Path $src)) { Fail "PLUSDE.dll not found: $src" }
$mod = [dnlib.DotNet.ModuleDefMD]::Load($src)
Log "target : $src"
Log "  runtime=$($mod.RuntimeVersion) ILOnly=$($mod.IsILOnly) (mixed-mode: native writer required)"
Log "provider: $($prov.Assembly.FullName)"

# ---- pre-flight ------------------------------------------------------------

$needed = @{}
foreach ($mr in $mod.GetMemberRefs()) {
    $decl = $mr.DeclaringType
    if ($null -eq $decl) { continue }
    $full = $decl.FullName
    $target = $null
    if ($MAP.ContainsKey($full)) { $target = $MAP[$full] }
    elseif ($full -like "$PROVIDER_NS.*") { $target = $full.Substring($PROVIDER_NS.Length + 1) }  # already patched
    if ($null -eq $target) { continue }
    $needed["$target::$($mr.Name.String)"] = $true
}

$missing = @()
foreach ($k in $needed.Keys) {
    $parts = $k -split '::'
    if (-not (Test-ProviderMember $parts[0] $parts[1])) { $missing += $k }
}
Log "pre-flight: $($needed.Count) distinct member reference(s) needed from the provider"
if ($missing.Count -gt 0) {
    foreach ($x in $missing) { Write-Host "  MISSING $x" -ForegroundColor Yellow }
    Fail "Provider is missing $($missing.Count) member(s) PLUSDE calls. Add them to GvrSqlite.cs and rebuild."
}
Log "pre-flight: PASS (provider covers every referenced member)"

if ($VerifyOnly) {
    $left = @($mod.GetTypeRefs() | Where-Object { $MAP.ContainsKey($_.FullName) })
    $swapped = @($mod.GetTypeRefs() | Where-Object { $_.Namespace -eq $PROVIDER_NS })
    Log "verify: $($swapped.Count) typeref(s) point at $PROVIDER_NS, $($left.Count) still point at SqlClient"
    foreach ($t in $swapped) { "   $($t.Namespace).$($t.Name)  [scope $($t.ResolutionScope.Name)]" }
    if ($left.Count -gt 0) { Fail "Some SqlClient typerefs were not swapped." }
    Log "verify: PASS"
    return
}

# ---- patch -----------------------------------------------------------------

# One AssemblyRef for the provider, matching what the compiler emitted for it.
$provAsm = $prov.Assembly
$asmRef = New-Object dnlib.DotNet.AssemblyRefUser($provAsm.Name, $provAsm.Version)
$asmRef = $mod.UpdateRowId($asmRef)

$n = 0
foreach ($tr in $mod.GetTypeRefs()) {
    $full = $tr.FullName
    if (-not $MAP.ContainsKey($full)) { continue }
    $newName = $MAP[$full]
    $tr.ResolutionScope = $asmRef
    $tr.Namespace = New-Object dnlib.DotNet.UTF8String($PROVIDER_NS)
    $tr.Name = New-Object dnlib.DotNet.UTF8String($newName)
    Log ("  swap {0,-46} -> {1}.{2}" -f $full, $PROVIDER_NS, $newName)
    $n++
}
if ($n -eq 0) { Fail "No SqlClient typerefs found -- is this the right PLUSDE.dll (or already patched)?" }

$outDir = Split-Path -Parent $OutputDll
if (!(Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

# NativeWrite keeps the native (C++/CLI) half of the mixed-mode image intact.
$opts = New-Object dnlib.DotNet.Writer.NativeModuleWriterOptions($mod, $true)
$opts.Logger = [dnlib.DotNet.DummyLogger]::NoThrowInstance
$mod.NativeWrite($OutputDll, $opts)
Log ("patched $n typeref(s) -> $OutputDll ({0:N0} bytes)" -f (Get-Item $OutputDll).Length)

# ---- verify ----------------------------------------------------------------

$chk = [dnlib.DotNet.ModuleDefMD]::Load($OutputDll)
$left = @($chk.GetTypeRefs() | Where-Object { $MAP.ContainsKey($_.FullName) })
$swapped = @($chk.GetTypeRefs() | Where-Object { $_.Namespace -eq $PROVIDER_NS })
Log "verify: $($swapped.Count) typeref(s) now resolve to $PROVIDER_NS; $($left.Count) SqlClient typeref(s) remain"
if ($left.Count -gt 0) { Fail "Patch incomplete." }
$refNames = ($chk.GetAssemblyRefs() | ForEach-Object { $_.Name.String }) -join ', '
Log "verify: assembly refs = $refNames"
Log "verify: PASS"
