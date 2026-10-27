<#
  Install-NASCAR.ps1 - what Install.bat runs. Asks which game to install or update and runs
  that game's installer with the same options:
    NASCAR Racing       (v1.1 disc, 050-0136-01)  -> Install-NASCAR-GVR-Portable.ps1
    NASCAR Team Racing  (2.0.2 disc)              -> Install-NASCAR-Team-Racing-Portable.ps1
  A mounted game disc is recognised and preselected: only the 2.0.2 disc has a P2P folder and a
  DATA3.CAB. Both games install side by side (their own folders, shortcuts and data).

    Install.bat -Game 1.1      (or 2.0) skip the question; every other option goes to the installer
#>
param([string]$Game = "")
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$scripts = @{ "1.1" = "Install-NASCAR-GVR-Portable.ps1"; "2.0" = "Install-NASCAR-Team-Racing-Portable.ps1" }
$titles  = @{ "1.1" = "NASCAR Racing"; "2.0" = "NASCAR Team Racing" }
$discs   = @{ "1.1" = "v1.1 disc (050-0136-01)"; "2.0" = "2.0.2 disc" }

# A mounted NASCAR game disc: which game, and where. $null when none is mounted.
function Find-GameDisc {
    foreach ($d in @(Get-PSDrive -PSProvider FileSystem -ErrorAction SilentlyContinue)) {
        $r = $d.Root
        try {
            if (!(Test-Path (Join-Path $r "data1.cab"))) { continue }
            $ini = Join-Path $r "GVRSETUP.INI"
            if (!(Test-Path $ini) -or ([IO.File]::ReadAllText($ini) -notmatch '(?m)^OSName=NASCAR')) { continue }
            $g = "1.1"
            if ((Test-Path (Join-Path $r "P2P")) -or (Test-Path (Join-Path $r "data3.cab"))) { $g = "2.0" }
            return @{ Game = $g; Root = $r }
        } catch { }
    }
    return $null
}

$noGui = @($args | Where-Object { "$_" -ieq "-NoGui" }).Count -gt 0
if ($Game -and !$scripts.ContainsKey($Game)) { Write-Host "-Game must be 1.1 or 2.0 (got '$Game')." -ForegroundColor Red; exit 1 }

if (!$Game) {
    $disc = Find-GameDisc
    $found = if ($disc) { "Found: the $($titles[$disc.Game]) $($discs[$disc.Game]) on $($disc.Root)" }
             else { "No game disc found. A new install needs the disc mounted; updating an existing install does not." }
    $canGui = !$noGui -and [Threading.Thread]::CurrentThread.GetApartmentState() -eq "STA"
    if ($canGui) {
        try {
            Add-Type -AssemblyName System.Windows.Forms, System.Drawing
            [Windows.Forms.Application]::EnableVisualStyles()
            $f = New-Object Windows.Forms.Form
            $f.Text = "NASCAR GVR - install or update"
            $f.FormBorderStyle = "FixedDialog"; $f.MaximizeBox = $false; $f.MinimizeBox = $false
            $f.StartPosition = "CenterScreen"; $f.TopMost = $true
            $f.ClientSize = New-Object Drawing.Size(460, 238)
            $f.Font = New-Object Drawing.Font("Segoe UI", 10)
            $q = New-Object Windows.Forms.Label
            $q.Text = "Which game do you want to install or update?"
            $q.Location = New-Object Drawing.Point(20, 16); $q.Size = New-Object Drawing.Size(420, 24)
            $f.Controls.Add($q)
            $y = 50
            foreach ($k in @("1.1", "2.0")) {
                $b = New-Object Windows.Forms.Button
                $b.Text = "$($titles[$k])  -  $($discs[$k])"
                $b.Location = New-Object Drawing.Point(20, $y); $b.Size = New-Object Drawing.Size(420, 44)
                $b.Tag = $k
                $b.Add_Click({ $f.Tag = $this.Tag; $f.Close() })
                if ($disc -and $disc.Game -eq $k) {     # the mounted disc's game: bold, focused, Enter picks it
                    $b.Font = New-Object Drawing.Font("Segoe UI", 10, [Drawing.FontStyle]::Bold)
                    $f.AcceptButton = $b
                }
                $f.Controls.Add($b)
                if ($disc -and $disc.Game -eq $k) { $f.ActiveControl = $b }
                $y += 54
            }
            $s = New-Object Windows.Forms.Label
            $s.Text = $found
            $s.Location = New-Object Drawing.Point(20, 162); $s.Size = New-Object Drawing.Size(420, 40)
            $s.ForeColor = [Drawing.Color]::DimGray
            $f.Controls.Add($s)
            $c = New-Object Windows.Forms.Button
            $c.Text = "Cancel"; $c.Location = New-Object Drawing.Point(340, 200); $c.Size = New-Object Drawing.Size(100, 28)
            $c.Add_Click({ $f.Tag = $null; $f.Close() })
            $f.CancelButton = $c
            $f.Controls.Add($c)
            [void]$f.ShowDialog()
            $Game = "$($f.Tag)"
            if (!$Game) { Write-Host "Cancelled - nothing was installed."; exit 0 }
        } catch {
            Write-Host "The game picker could not be shown ($($_.Exception.Message)) - asking here instead."
            $canGui = $false
        }
    }
    if (!$canGui -and !$Game) {
        Write-Host ""
        Write-Host "Which game do you want to install or update?" -ForegroundColor Cyan
        Write-Host "  1  $($titles['1.1'])  -  $($discs['1.1'])"
        Write-Host "  2  $($titles['2.0'])  -  $($discs['2.0'])"
        Write-Host "  $found"
        while (!$Game) {
            $a = (Read-Host "Type 1 or 2").Trim()
            if ($a -eq "1") { $Game = "1.1" } elseif ($a -eq "2") { $Game = "2.0" }
        }
    }
}

$script = Join-Path $here $scripts[$Game]
if (!(Test-Path -LiteralPath $script)) {
    Write-Host "$($scripts[$Game]) is missing from this folder - unzip the whole release again (an antivirus may have removed it)." -ForegroundColor Red
    exit 1
}
Write-Host "Installing / updating $($titles[$Game]) ..." -ForegroundColor Cyan
# a fresh PowerShell, exactly as Install.bat used to start each installer (STA for its folder pickers)
& powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File $script @args
exit $LASTEXITCODE
