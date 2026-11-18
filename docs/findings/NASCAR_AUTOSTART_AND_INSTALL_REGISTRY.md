# NASCAR: what starts at boot, and every registry key the OEM install writes

Mapped 2026-10-03. The page covers two layers, read from two sources:

| Layer | Source |
|---|---|
| A. The cabinet OS (before the game) | The recovery image's own hives: `Extracted\RecoveryImage\WINDOWS\system32\config\*` and `Documents and Settings\*\NTUSER.DAT` |
| B. The OEM game install (on top of A) | `GAME ISO EXTRACTED\`: `setup.inx`, `GVRSETUP.INI`, `Registry\*.reg`, `Config\*`, `DbSetup\PGA.msi` |

`Extracted\RecoveryImage` is a complete extraction of `NASCAR-20080826-1.GVR`. That file is a plain
WIM of XPe SP2 build 2600.2180. All 4684 files match the WIM manifest by path and size. The 22 hive
files are byte-identical to the WIM's: `SYSTEM`, `SOFTWARE`, `SAM`, `SECURITY`, `DEFAULT` with
their `.LOG`s, and every `NTUSER.DAT` and `UsrClass.dat`. Re-extracting would gain nothing.

All hive reads use `Tools\Dump-Hive.py`, with no `reg load` and no admin rights.

---

## A. The cabinet OS: autostart in the recovery image

### A.1 `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`

| Name | Command |
|---|---|
| `HideTaskbar` | `C:\GVR\GVR_Tools\HideTaskBar.exe` |
| `netset` | `C:\GVR\GVR_Tools\netset.exe` |
| `RTHDCPL` / `Alcmtr` | Realtek HD audio panel / jack monitor |
| `High Definition Audio Property Page Shortcut` | `HDAShCut.exe` |
| `Cmaudio` | `RunDll32 cmicnfg.cpl,CMICtrlWnd` (C-Media) |
| `Tweak UI` | `RUNDLL32.EXE TWEAKUI.CPL,TweakMeUp` |

### A.2 Run-once and first boot

| Where | Value | Meaning |
|---|---|---|
| `...\CurrentVersion\RunOnce` | `CDS = C:\GVR\GVR_Tools\CDS.exe /t:10` | First logon after a restore. Identical to `GVR\GVR_Tools\cds-runonce.reg` in the image. |
| `...\CurrentVersion\RunOnce\Setup` | Two Tweak UI first-run entries | Vendor noise |
| `SYSTEM\ControlSet001\Control\Session Manager` | `SetupExecute = setupcn.exe, setupcl.exe` | XPe re-seal and SID change on the first boot of a restored image. `GVR_Tools\fbreseal.exe` arms it. |
| Same | `BootExecute` = empty | No native boot-time programs |

Absent: `RunOnceEx`, `RunServices(Once)`, `Policies\Explorer\Run`, and any per-user `Run` or
`RunOnce`. `AppInit_DLLs` is empty.

### A.3 Logon: `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon`

Values: `AutoAdminLogon = 1`, `DefaultUserName = cabinet`, `Shell = Explorer.exe`, `Userinit`
stock, `AutoRestartShell = 1`, `DisableCAD = 1`, `DontDisplayLastUserName = 1`.

`Shell` is the stock value: the cabinet hides Explorer rather than replacing it. No NTUSER.DAT
has a per-user `Shell` override.

### A.4 The `cabinet` user's lockdown policies

`cabinet\NTUSER.DAT` holds 22 values under
`Software\Microsoft\Windows\CurrentVersion\Policies\Explorer`:
`NoClose`, `HideClock`, `LockTaskbar`, `NoSetTaskbar`, `NoToolbarsOnTaskbar`, `NoTrayItemsDisplay`,
`NoFind`, `NoSetFolders`, `ClassicShell`, `ForceClassicControlPanel`, `NoSimpleStartMenu`,
`NoStartMenuMorePrograms`, `StartMenuLogoff`, `NoWelcomeScreen`, `NoDesktopCleanupWizard`,
`NoNetworkConnections`, `NoRecentDocsHistory`, `NoRecentDocsMenu`, `NoSMHelp`, `NoSMMyDocs`,
`NoSMMyPictures`, `NoDriveTypeAutoRun = 0x91`.

`Administrator` and `Default User` carry only four: `NoDriveTypeAutoRun`, `StartMenuLogoff`,
`NoStartMenuMorePrograms` and `NoTrayItemsDisplay`.

### A.5 Startup folders

The only entry is `All Users\Start Menu\Programs\Startup\Service Manager.lnk`. It starts
`C:\Program Files\Microsoft SQL Server\80\Tools\Binn\sqlmangr.exe`, the MSDE tray icon.

### A.6 Auto-start services beyond stock XP

Extra auto-start services: `Hardlock` and `Haspnt` for the dongle, `MSSQLSERVER` for MSDE,
`MSMQ`, `SNMP`, `RemoteRegistry`, `NwlnkIpx`, `NwlnkSpx` and `NwlnkNb` for IPX/SPX, and `mdmxsdk`.

**No GlobalVR service exists.** Every GVR process starts from `Run`, from the Startup folder or
from Hercules' `GVRCrashMonitor` (B.3).

---

## B. The OEM game install: every registry write, by step

### B.1 `setup.inx` (InstallShield 12)

1. It reads `HKLM\SYSTEM\ControlSet001\Control\Session Manager\Environment\RUNTIMEOEMREV` and
   compares it with `GVRSETUP.INI [MINIMUMOS]`. If the check fails, it stops with "Required
   registry elements missing!".
2. It copies the five file groups.
3. Under the status text "Applying registry settings...", it imports **every** `Registry\*.reg`.
   It uses the file mask `*.reg` and `RegEdt32.exe`. This is not a curated subset: all nine files
   below land, including `ConfigEventLog.reg` and `tsunami.reg`.
4. It writes InstallShield's own maintenance registration under
   `Software\Microsoft\Windows\CurrentVersion\Uninstall\` and `...\App Paths\`. The product GUID
   is `70AA52E7-89A2-428E-8C92-3ABC73B83F04`, from `Setup.ini`.
5. It runs `GVRSETUP.INI [LAUNCHAPPS]` (B.4).

### B.2 The nine `.reg` files

| File | Keys written |
|---|---|
| `NASCAR.reg` | `HKLM\SOFTWARE\GlobalVR\NASCAR`: `Version=1.1.0`, `Build=163`, `CommVersion=1.1.0`, `Prefix`, `Suffix`, `FullScreenWidth=1360`, `FullScreenHeight=768`, `FourButtonAbort=1`, `StallMonitorTest=0`. Also `HKCU\Control Panel\Cursors`, set to the `GvrCurs` scheme (`%SYSTEMROOT%\cursors\*_m.cur`, copied by the `WINDOWS` file group). |
| `NASCAR_Shell.reg` | `HKLM\SOFTWARE\GlobalVR\NASCAR`: `cmd="-startPos 43 "`, `wtf=""`, `launchName=NASCAR_GVR.exe`, `launchFolder=C:\NASCAR\Game`, `attractName=NASCAR_Attract.am`, `attractFolder=C:\NASCAR\shell`, `selectName=NASCAR_Selection.am`, `selectFolder=C:\NASCAR\shell`, `AttractAudioCounter=0`, `MusicVolume=70`, `SimpleAttract=0`. This is the only ANSI `.reg`. The rest are UTF-8 with BOM. |
| `NASCAR_GvrIO.reg` | `HKLM\SOFTWARE\gvr\Plus\2.0\Cabinet\GameRoot = C:\NASCAR\Game\` |
| `Schema.reg` | `HKLM\SOFTWARE\gvr\Plus\1.1\Cabinet`: `PlusSchemaPath=C:\GvrPlus\4\schema\NASCARcabinetXml.enc`, `PublicKeyPath`, `PromotionPath`, `PatchDownloadPath=C:\PatchService\`, `WebServerIP=192.168.1.39`, retry and threshold values |
| `GvrShell.reg` | `HKCU\Software\Anark\Client\3.0\Preferences`: `Initialized=1`, `DisableGL="1"`, `EnableScriptDebugger=1`, `DebugMode=0`, update-check values |
| `HerculesCab.reg` | `HKLM\SOFTWARE\Gvr\hercules\…`: coin-meter masks, `GVRVolumeMonitor\Volume=0x3a`, coin sound, and `GVRCrashMonitor\Prog00..06` (B.3) |
| `Startup.reg` | `HKLM\...\CurrentVersion\Run\GVRHercules = c:\hercules\gvrboot.exe`. Three more entries are commented out: setvideo, hidetaskbar, NFSSetDesktop2. |
| `ConfigEventLog.reg` | `Retention=0` on the Application, Security and System event logs, in both `CurrentControlSet` and `ControlSet002` |
| `tsunami.reg` | `HKLM\SOFTWARE\Tsunami`: `MotionLib`, `MotionTune`, `MotionType=4`, `MotionScale` |

`NASCAR\InstallGVR\` in the cabinet payload holds byte-identical copies plus `FinalizeCabinet.bat`.
That batch file is the manual flow from before the installer existed (2007-01). It ran
`regedit /s` on the same files, plus a `Divx.reg` that never shipped.

### B.3 How the cabinet actually boots into the game

Registry and filesystem together:

```
logon (autologon as cabinet)
 ├─ HKLM Run\GVRHercules  → c:\hercules\GVRBoot.exe
 │     └─ GVRCrashMonitor reads HKLM\SOFTWARE\Gvr\hercules\GVRCrashMonitor\Prog00..06:
 │          Prog00 GVRDongleMonitor.exe   (enabled=3)
 │          Prog01 GVRCoinMonitor.exe
 │          Prog02 GVRStallMonitor.exe
 │          Prog03 GVRCacheWarmer.exe      (enabled=0)
 │          Prog04 AMPlayer.exe c:\NASCAR\Shell\shell.am -v FULL -cabinet   (-minvol -1500)
 │          Prog05 GvrCoinSound.exe
 │          Prog06 GvrVolumeMonitor.exe
 ├─ All Users Startup\LoadingShell.lnk → C:\NASCAR\Shell\LoadingShell.exe   (from Shortcut.wsf)
 ├─ All Users Startup\HideTaskbar.lnk  → C:\NASCAR\Shell\HideTaskbar.exe    (from Shortcut.wsf)
 └─ HKLM Run\HideTaskbar, netset       (already in the recovery image, A.1)
AMPlayer shell → reads HKLM\SOFTWARE\GlobalVR\NASCAR launchFolder/launchName/cmd
               → NASCAR_GVR.exe -startPos 43 -track <chosen>
```

The front end starts from **Hercules' crash monitor**, not from a registry Run entry of its own.
The loading screen and the second taskbar hider are Startup-folder shortcuts, not registry entries.

`Hercules\HerculesSetup.reg` in the payload is not imported, because only `Registry\*.reg` is. It
is a PGA Tour Golf leftover (`path=c:\PGA_TOUR_GOLF_2006\Shell\bin`). So is `Hercules\stopall.bat`
(`killapps pga_tour_golf_2005`, `univershell2`).

### B.4 `GVRSETUP.INI [LAUNCHAPPS]`: what each step touches

| # | Step | Registry / system effect |
|---|---|---|
| 0 | `wscript Config\Shortcut.wsf` | Filesystem only. The All Users Start Menu gets `gvrShutdown.lnk`, which points to `C:\GVR\GVR_Tools\CloseApps.bat`, and `gvrStartup.lnk`, which points to `C:\Hercules\GVRBoot.exe`. The All Users Startup folder gets `LoadingShell.lnk` and `HideTaskbar.lnk`. |
| 1 | `SetVolume.exe 50` | System mixer volume |
| 2 | `Config\nascarfw.exe` | Runs `gvrNetworkConfig.bat`: DHCP on `local`, firewall on, 4 UDP ports opened. Windows stores all of these in the registry. |
| 3 | `rundll32 NvCpl.dll,dtcfg setdvc all 20` | NVIDIA Digital Vibrance, persisted by the driver |
| 4 | `msiexec /i DbSetup\PGA.msi /quiet` | See B.5 |
| 5 | `pathman.exe /as C:\NASCAR\SHELL\BIN` | Appends to the machine `Path` in `HKLM\SYSTEM\...\Session Manager\Environment` |
| 6 | `DbSetup\Nascar_db.exe` | SQL schema and pricing load (database, not registry) |

### B.5 `PGA.msi` ("PGA" 1.0.0, Global VR, ProductCode `{C6BB203E-7BE8-49C0-B0C8-94BED980F3CA}`)

It has no `Registry` table. All its registry effects come from three EXE custom actions:

| Custom action | When | Effect |
|---|---|---|
| `GvrPlusExportDatabaseScript.exe` | install (seq 5998) | Reads `HKLM\SOFTWARE\Gvr\Plus\1.1\Cabinet\PlusSchemaPath`. Builds the DB via `OSQL.exe` and rotates `sa`. On success it **creates `HKLM\SOFTWARE\Gvr\Installer\DeskTopEngine\Exists = 1`**. If that key already exists, the run counts as an upgrade: SQL is restarted and `db_propagate.exe -recover` preserves data. |
| `UnregDlls.exe`, which runs `UnregisterDlls.bat` | install (seq 5999) | `regasm /Unregister` and `gacutil /u`, then `regasm` and `gacutil -i`, on `GvrPeUtil`, `GvrPeSchemaUtil`, `GvrPeClient`, `GvrPeEngine`, `GvrPeDialer` (`C:\GvrPlus\4\lib`). The result is COM registration under `HKCR` and entries in the .NET 1.1 GAC. |
| `DeleteRegistry.exe` | uninstall (seq 1699) | `DeleteSubKeyTree("SOFTWARE\Gvr\Installer\")` |

Windows Installer also writes its own product registration.

---

## C. What a game-only install keeps

| Keep (repointed at the install root) | Never reproduce |
|---|---|
| `GlobalVR\NASCAR` from `NASCAR.reg` + `NASCAR_Shell.reg` | `Run\GVRHercules`, `Run\HideTaskbar`, `Run\netset`, `RunOnce\CDS` |
| `gvr\Plus\2.0\Cabinet\GameRoot` | Startup `LoadingShell.lnk` / `HideTaskbar.lnk`, Start Menu `gvrStartup` / `gvrShutdown` |
| `gvr\Plus\1.1\Cabinet\*` (minus `WebServerIP`) | `Gvr\hercules\*` (`HerculesCab.reg`) |
| Anark prefs (`GvrShell.reg`) when the shell is installed | `ConfigEventLog.reg`, `tsunami.reg`, cursor scheme (opt-in only) |
| | Autologon, `DisableCAD`, the `cabinet` user's 22 Explorer policies, `pathman` PATH edit |

`Gvr\Installer\DeskTopEngine` only matters if the OEM `GvrPlusExportDatabaseScript` is re-run. The
SQLite route does not use it.

## Corrections to older notes

- `NASCAR_INSTALL_ANALYSIS.md` §1 described `Shortcut.wsf` as only creating the "Close Apps"
  shortcut. It also creates `gvrStartup.lnk` and the **two Startup-folder autostarts**
  (`LoadingShell.lnk`, `HideTaskbar.lnk`).
- `NASCAR_REGISTRY.md` §4 said nothing GVR-specific survives in `cabinet\NTUSER.DAT` beyond
  MUICache. The 22 Explorer lockdown policies (A.4) are there.
