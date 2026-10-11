<#
  Check-DotNet11.ps1 - read-only report of the .NET Framework 1.1 install (support tool).

  The NASCAR shell hangs inside .NET 1.1's own startup when that install is damaged; a clean
  reinstall of .NET 1.1 fixed it for a player (2026-10-12). This lists what a working 1.1 has, so a
  damaged one shows which part is missing. Changes nothing. Run in PowerShell:

    powershell -ExecutionPolicy Bypass -File Check-DotNet11.ps1 > dotnet11-report.txt
#>
$fw = Join-Path $env:WINDIR "Microsoft.NET\Framework\v1.1.4322"
"=== .NET 1.1 report  $(Get-Date -Format 'yyyy-MM-dd HH:mm')  $([Environment]::OSVersion.VersionString)"
"--- runtime files ($fw)"
foreach ($n in "mscorlib.dll", "mscorwks.dll", "mscorsvr.dll", "mscorjit.dll", "mscorrc.dll", "fusion.dll",
               "System.dll", "System.Data.dll", "System.Xml.dll", "System.EnterpriseServices.dll", "regsvcs.exe", "csc.exe") {
    $p = Join-Path $fw $n
    if (Test-Path $p) { $v = (Get-Item $p).VersionInfo; "  {0,-32} {1,-16} {2,10} bytes  {3}" -f $n, $v.FileVersion, (Get-Item $p).Length, $v.Language }
    else { "  {0,-32} MISSING" -f $n }
}
"  localized folders: " + ((Get-ChildItem $fw -ErrorAction SilentlyContinue | Where-Object { $_.PSIsContainer } | ForEach-Object { $_.Name }) -join ", ")
"--- global assembly cache (1.0.5000.0 = the 1.1 assemblies)"
$gac = Join-Path $env:WINDIR "assembly\GAC"
foreach ($a in "System", "System.Data", "System.Xml", "System.Drawing", "System.Windows.Forms", "System.EnterpriseServices", "Microsoft.VisualBasic") {
    $d = Join-Path $gac "$a\1.0.5000.0__b77a5c561934e089"
    if (!(Test-Path $d)) { $d = Join-Path $gac "$a\1.0.5000.0__b03f5f7f11d50a3a" }
    "  {0,-28} {1}" -f $a, $(if (Test-Path (Join-Path $d "$a.dll")) { "present" } else { "MISSING" })
}
"  native images (ngen): " + ((Get-ChildItem (Join-Path $env:WINDIR "assembly") -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "NativeImages1_v1.1.4322*" } | ForEach-Object { $_.Name }) -join ", ")
"--- registry"
foreach ($k in "HKLM:\SOFTWARE\Microsoft\.NETFramework", "HKLM:\SOFTWARE\WOW6432Node\Microsoft\.NETFramework") {
    if (!(Test-Path $k)) { "  $k  MISSING"; continue }
    $p = Get-ItemProperty $k
    "  $k : InstallRoot=$($p.InstallRoot)  DbgJITDebugLaunchSetting=$($p.DbgJITDebugLaunchSetting)  OnlyUseLatestCLR=$($p.OnlyUseLatestCLR)"
    "    policy v1.1: " + $(if (Test-Path "$k\policy\v1.1") { ((Get-ItemProperty "$k\policy\v1.1").PSObject.Properties | Where-Object { $_.Name -match '^\d' } | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join " " } else { "MISSING" })
}
foreach ($k in "HKLM:\SOFTWARE\Microsoft\NET Framework Setup\NDP\v1.1.4322", "HKLM:\SOFTWARE\WOW6432Node\Microsoft\NET Framework Setup\NDP\v1.1.4322") {
    if (Test-Path $k) { $p = Get-ItemProperty $k; "  $k : Install=$($p.Install) SP=$($p.SP)" } else { "  $k  (absent)" }
}
foreach ($k in "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{CB2F7EDD-9D1F-43C1-90FC-4F52EAE172A1}",
               "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{CB2F7EDD-9D1F-43C1-90FC-4F52EAE172A1}") {
    if (Test-Path $k) { $p = Get-ItemProperty $k; "  uninstall entry: $($p.DisplayName) $($p.DisplayVersion)  ($k)" }
}
"--- regional format"
$i = Get-ItemProperty "HKCU:\Control Panel\International"
"  LocaleName=$($i.LocaleName) Locale=$($i.Locale) sDecimal='$($i.sDecimal)' sThousand='$($i.sThousand)' sCurrency='$($i.sCurrency)' sGrouping='$($i.sGrouping)'"
"=== end"
