<#
.SYNOPSIS
    Forces GvrPlusDEPlugin's dongle check to succeed, so a GlobalVR shell runs
    without its hardware dongle.

.DESCRIPTION
    WHAT THE GATE ACTUALLY IS
    The shell's scene scripts call IsValidDongle(), exposed by the managed
    plug-in GvrPlusDEPlugin.dll. Decompiled, it is:

        GvrSmartDevice dev(engine, gameName, GVRSDType=2);   // 2 = DONGLE
        if (dev.DongleInserted(0)) {
            dev.ReadHeader(&err);
            GvrMemObject* mo = dev.GetMemObject(&err, "Header.GameName");
            if (mo) {
                char buf[9]; mo->GetBuffer(buf, 9); buf[8] = 0;
                if (strstr(buf, gameName)) return true;      // only success path
            }
        }
        return false;                                        // -> DongleError.am

    WHY NOTHING BELOW THIS CAN FIX IT
    "Header.GameName" is defined NOWHERE on a cabinet without a dongle - it
    appears in exactly one file in the whole install, GvrPlusDEPlugin.dll
    itself, as the literal it asks for. The field definition lives on the real
    dongle's own schema. So GetMemObject returns null and IsValidDongle returns
    false regardless of the storage device underneath.

    That was established the hard way: replacing GVRSCR28.dll with a software
    device that reports IsPresent=true, GetType=2 (DONGLE) and a working GetId
    changed nothing, and its trace showed the shell never calling GetType,
    GetId, Connect or Read at all - the check fails upstream of the device.
    Provisioning the Plus database did not help either; a SQL trace showed 32
    GlobalVariable reads and no dongle query whatsoever.

    So the only honest place to intervene is the gate itself.

    HOW
    GvrPlusDEPlugin.dll is a mixed-mode C++/CLI assembly, so it must be written
    back with dnlib's NATIVE writer or the native half of the image is lost -
    the same constraint as Patch-PlusdeToSqlite.ps1. The named methods get their
    IL bodies replaced with "ldc.i4.1; ret".

    The OEM file is preserved as <name>_oem.dll and the patch is idempotent.

.PARAMETER PluginPath
    GvrPlusDEPlugin.dll to patch (defaults to the NASCAR install's shell).

.PARAMETER Methods
    Static boolean methods to force true. Defaults to IsValidDongle.

.PARAMETER DryRun
    Report what would change and write nothing.

.EXAMPLE
    .\Patch-GvrDongleCheck.ps1 -PluginPath "D:\Games\NASCAR\Shell\bin\plugins\GvrPlusDEPlugin.dll"
    .\Patch-GvrDongleCheck.ps1 -DryRun
#>
[CmdletBinding()]
param(
    [string]$PluginPath = "D:\Games\NASCAR\Shell\bin\plugins\GvrPlusDEPlugin.dll",
    [string[]]$Methods  = @("IsValidDongle"),
    [string]$DnlibPath  = "",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
trap { Write-Host "FAILED: $_" -ForegroundColor Red; exit 1 }

function Log($m)  { Write-Host $m }
function Fail($m) { throw $m }

if (!(Test-Path -LiteralPath $PluginPath)) { Fail "plug-in not found: $PluginPath" }
if ([string]::IsNullOrEmpty($DnlibPath)) {
    $DnlibPath = "D:\NFSU_GVR\Tools\lib\dnlib.net45.dll"
}
if (!(Test-Path -LiteralPath $DnlibPath)) { Fail "dnlib (net45 build) not found at $DnlibPath" }

Add-Type -Path $DnlibPath

$dir  = Split-Path -Parent $PluginPath
$name = [IO.Path]::GetFileNameWithoutExtension($PluginPath)
$oem  = Join-Path $dir "${name}_oem.dll"

# Always patch FROM the pristine OEM copy, so re-running cannot stack patches.
if (!(Test-Path -LiteralPath $oem)) {
    Log "preserving OEM -> $oem"
    if (!$DryRun) { Copy-Item -LiteralPath $PluginPath -Destination $oem -Force }
    $src = $PluginPath
} else {
    Log "OEM already preserved; patching from $oem"
    $src = $oem
}

$mod = [dnlib.DotNet.ModuleDefMD]::Load($src)
Log "loaded $([IO.Path]::GetFileName($src))  (IsILOnly=$($mod.IsILOnly))"
if ($mod.IsILOnly) { Log "  note: image reports IL-only; the native writer still handles it" }

$found = 0
foreach ($mname in $Methods) {
    $hits = @()
    foreach ($t in $mod.GetTypes()) {
        foreach ($m in $t.Methods) {
            if ($m.Name -ne $mname) { continue }
            if (!$m.HasBody) { continue }
            # Only touch a parameterless static returning a boolean - that is the
            # shape of these gates, and refusing anything else stops a typo from
            # silently corrupting an unrelated method.
            if (!$m.IsStatic -or $m.Parameters.Count -ne 0) { continue }
            # C++/CLI decorates the return type with calling-convention modopts,
            # e.g. "System.Boolean modopt(...CallConvCdecl)", so an exact-match
            # test on "System.Boolean" silently finds nothing.
            if (!$m.ReturnType.FullName.StartsWith("System.Boolean")) { continue }
            $hits += $m
        }
    }
    if ($hits.Count -eq 0) { Log "  $mname : NOT FOUND (static, no args, returns bool)"; continue }
    foreach ($m in $hits) {
        $n = $m.Body.Instructions.Count
        Log "  $($m.DeclaringType.FullName)::$($m.Name)  body=$n instruction(s) -> return true"
        if ($DryRun) { continue }
        $body = New-Object dnlib.DotNet.Emit.CilBody
        $body.Instructions.Add([dnlib.DotNet.Emit.Instruction]::Create([dnlib.DotNet.Emit.OpCodes]::Ldc_I4_1))
        $body.Instructions.Add([dnlib.DotNet.Emit.Instruction]::Create([dnlib.DotNet.Emit.OpCodes]::Ret))
        $body.MaxStack = 1
        # The original has try/fault handlers and locals; a replacement body must
        # carry none of them or the writer emits an invalid method.
        $m.Body = $body
        $found++
    }
}

if ($DryRun) { Log ""; Log "dry run - nothing written."; exit 0 }
if ($found -eq 0) { Fail "no methods were patched - refusing to write an unchanged copy" }

# NativeWrite keeps the native (C++/CLI) half of the mixed-mode image intact.
$opts = New-Object dnlib.DotNet.Writer.NativeModuleWriterOptions($mod, $true)
$opts.Logger = [dnlib.DotNet.DummyLogger]::NoThrowInstance
$tmp = Join-Path $env:TEMP ("$name.patched.dll")
$mod.NativeWrite($tmp, $opts)
$mod.Dispose()

# Verify the result loads and the bodies really are two instructions.
$chk = [dnlib.DotNet.ModuleDefMD]::Load($tmp)
$ok = 0
foreach ($t in $chk.GetTypes()) {
    foreach ($m in $t.Methods) {
        if ($Methods -contains $m.Name -and $m.HasBody -and $m.Body.Instructions.Count -eq 2) { $ok++ }
    }
}
$chk.Dispose()
if ($ok -eq 0) { Fail "verification failed: patched bodies not found in $tmp" }

# dnlib memory-maps whatever it loads. Without forcing the finalizers the live
# DLL can still have a mapped section open and the copy below fails with
# "cannot be performed on a file with a user-mapped section open".
[GC]::Collect(); [GC]::WaitForPendingFinalizers(); [GC]::Collect()

Copy-Item -LiteralPath $tmp -Destination $PluginPath -Force
Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue

Log ""
Log "patched $found method(s); verified $ok."
Log "  live : $PluginPath"
Log "  OEM  : $oem   (restore by copying this back)"
