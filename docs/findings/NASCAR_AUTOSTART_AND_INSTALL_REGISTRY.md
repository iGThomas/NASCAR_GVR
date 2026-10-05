# NASCAR: what starts at boot, and every registry key the OEM install writes

Mapped 2026-10-03. Two layers, read from two sources:

| Layer | Source |
|---|---|
| **A. The cabinet OS** (before the game) | the recovery image's own hives — `Extracted\RecoveryImage\WINDOWS\system32\config\*` and `Documents and Settings\*\NTUSER.DAT` |
| **B. The OEM game install** (on top of A) | `GAME ISO EXTRACTED\` — `setup.inx`, `GVRSETUP.INI`, `Registry\*.reg`, `Config\*`, `DbSetup\PGA.msi` |

`Extracted\RecoveryImage` is a **complete** extraction of `NASCAR-20080826-1.GVR` (a plain WIM,
XPe SP2 build 2600.2180): all 4684 files match the WIM manifest by path and size, and the 22
hive files (`SYSTEM`, `SOFTWARE`, `SAM`, `SECURITY`, `DEFAULT` + `.LOG`s, every `NTUSER.DAT` /
`UsrClass.dat`) are byte-identical to the WIM's. Re-extracting would gain nothing.

All hive reads use `Tools\Dump-Hive.py` (no `reg load`, no admin).

---

## A. The cabinet OS — autostart in the recovery image

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
| `...\CurrentVersion\RunOnce` | `CDS = C:\GVR\GVR_Tools\CDS.exe /t:10` | first logon after a restore; identical to `GVR\GVR_Tools\cds-runonce.reg` in the image |
| `...\CurrentVersion\RunOnce\Setup` | two Tweak UI first-run entries | vendor noise |
| `SYSTEM\ControlSet001\Control\Session Manager` | `SetupExecute = setupcn.exe, setupcl.exe` | XPe re-seal / SID change on the first boot of a restored image (`GVR_Tools\fbreseal.exe` arms it) |
| same | `BootExecute` = empty | no native boot-time programs |

Absent: `RunOnceEx`, `RunServices(Once)`, `Policies\Explorer\Run`, any per-user `Run`/`RunOnce`.
`AppInit_DLLs` is empty.

### A.3 Logon — `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon`

`AutoAdminLogon = 1`, `DefaultUserName = cabinet`, `Shell = Explorer.exe` (stock — the cabinet
hides Explorer rather than replacing it), `Userinit` stock, `AutoRestartShell = 1`,
`DisableCAD = 1`, `DontDisplayLastUserName = 1`. No per-user `Shell` override in any NTUSER.DAT.

### A.4 The `cabinet` user's lockdown policies

`cabinet\NTUSER.DAT` → `Software\Microsoft\Windows\CurrentVersion\Policies\Explorer`, 22 values:
`NoClose`, `HideClock`, `LockTaskbar`, `NoSetTaskbar`, `NoToolbarsOnTaskbar`, `NoTrayItemsDisplay`,
`NoFind`, `NoSetFolders`, `ClassicShell`, `ForceClassicControlPanel`, `NoSimpleStartMenu`,
`NoStartMenuMorePrograms`, `StartMenuLogoff`, `NoWelcomeScreen`, `NoDesktopCleanupWizard`,
`NoNetworkConnections`, `NoRecentDocsHistory`, `NoRecentDocsMenu`, `NoSMHelp`, `NoSMMyDocs`,
`NoSMMyPictures`, `NoDriveTypeAutoRun = 0x91`.
`Administrator` and `Default User` carry only four (`NoDriveTypeAutoRun`, `StartMenuLogoff`,
`NoStartMenuMorePrograms`, `NoTrayItemsDisplay`).

### A.5 Startup folders

Only `All Users\Start Menu\Programs\Startup\Service Manager.lnk` →
`C:\Program Files\Microsoft SQL Server\80\Tools\Binn\sqlmangr.exe` (MSDE tray icon).

### A.6 Auto-start services beyond stock XP

`Hardlock`, `Haspnt` (dongle drivers), `MSSQLSERVER` (MSDE), `MSMQ`, `SNMP`, `RemoteRegistry`,
`NwlnkIpx` / `NwlnkSpx` / `NwlnkNb` (IPX/SPX), `mdmxsdk`. **No GlobalVR service exists** — every GVR
process starts from `Run`, the Startup folder, or Hercules' `GVRCrashMonitor` (B.3).

---

## B. The OEM game install — every registry write, by step

### B.1 `setup.inx` (InstallShield 12)

1. **Reads** `HKLM\SYSTEM\ControlSet001\Control\Session Manager\Environment\RUNTIMEOEMREV` and
   compares it with `GVRSETUP.INI [MINIMUMOS]` (fails with "Required registry elements missing!").
2. Copies the five file groups.
3. **"Applying registry settings..."** — imports **every** `Registry\*.reg` (file mask `*.reg`,
   via `RegEdt32.exe`). Not a curated subset: all nine files below land, including
   `ConfigEventLog.reg` and `tsunami.reg`.
4. InstallShield's own maintenance registration under
   `Software\Microsoft\Windows\CurrentVersion\Uninstall\` and `...\App Paths\`
   (product GUID `70AA52E7-89A2-428E-8C92-3ABC73B83F04`, from `Setup.ini`).
5. Runs `GVRSETUP.INI [LAUNCHAPPS]` (B.4).

### B.2 The nine `.reg` files

| File | Keys written |
|---|---|
| `NASCAR.reg` | `HKLM\SOFTWARE\GlobalVR\NASCAR`: `Version=1.1.0`, `Build=163`, `CommVersion=1.1.0`, `Prefix`, `Suffix`, `FullScreenWidth=1360`, `FullScreenHeight=768`, `FourButtonAbort=1`, `StallMonitorTest=0` — **plus** `HKCU\Control Panel\Cursors` → the `GvrCurs` scheme (`%SYSTEMROOT%\cursors\*_m.cur`, copied by the `WINDOWS` file group) |
| `NASCAR_Shell.reg` | `HKLM\SOFTWARE\GlobalVR\NASCAR`: `cmd="-startPos 43 "`, `wtf=""`, `launchName=NASCAR_GVR.exe`, `launchFolder=C:\NASCAR\Game`, `attractName=NASCAR_Attract.am`, `attractFolder=C:\NASCAR\shell`, `selectName=NASCAR_Selection.am`, `selectFolder=C:\NASCAR\shell`, `AttractAudioCounter=0`, `MusicVolume=70`, `SimpleAttract=0` (the only ANSI `.reg`; the rest are UTF-8 with BOM) |
| `NASCAR_GvrIO.reg` | `HKLM\SOFTWARE\gvr\Plus\2.0\Cabinet\GameRoot = C:\NASCAR\Game\` |
| `Schema.reg` | `HKLM\SOFTWARE\gvr\Plus\1.1\Cabinet`: `PlusSchemaPath=C:\GvrPlus\4\schema\NASCARcabinetXml.enc`, `PublicKeyPath`, `PromotionPath`, `PatchDownloadPath=C:\PatchService\`, `WebServerIP=192.168.1.39`, retry/threshold values |
| `GvrShell.reg` | `HKCU\Software\Anark\Client\3.0\Preferences`: `Initialized=1`, `DisableGL="1"`, `EnableScriptDebugger=1`, `DebugMode=0`, update-check values |
| `HerculesCab.reg` | `HKLM\SOFTWARE\Gvr\hercules\…`: coin-meter masks, `GVRVolumeMonitor\Volume=0x3a`, coin sound, and `GVRCrashMonitor\Prog00..06` (B.3) |
| `Startup.reg` | `HKLM\...\CurrentVersion\Run\GVRHercules = c:\hercules\gvrboot.exe` (three more entries — setvideo, hidetaskbar, NFSSetDesktop2 — are commented out) |
| `ConfigEventLog.reg` | `Retention=0` on Application/Security/System event logs, in `CurrentControlSet` **and** `ControlSet002` |
| `tsunami.reg` | `HKLM\SOFTWARE\Tsunami`: `MotionLib`, `MotionTune`, `MotionType=4`, `MotionScale` |

`NASCAR\InstallGVR\` in the cabinet payload holds byte-identical copies plus `FinalizeCabinet.bat`
— the pre-installer manual flow (2007-01), which `regedit /s`'d the same files plus a `Divx.reg`
that never shipped.

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

So the front end's autostart is **Hercules' crash monitor, not a registry Run entry of its own**,
and the loading screen + second taskbar-hider are **Startup-folder shortcuts**, not registry.

`Hercules\HerculesSetup.reg` in the payload is **not** imported (only `Registry\*.reg` is) — it is
a PGA Tour Golf leftover (`path=c:\PGA_TOUR_GOLF_2006\Shell\bin`), as is `Hercules\stopall.bat`
(`killapps pga_tour_golf_2005`, `univershell2`).

### B.4 `GVRSETUP.INI [LAUNCHAPPS]` — what each step touches

| # | Step | Registry / system effect |
|---|---|---|
| 0 | `wscript Config\Shortcut.wsf` | **filesystem**: All Users Start Menu `gvrShutdown.lnk` (→ `C:\GVR\GVR_Tools\CloseApps.bat`) and `gvrStartup.lnk` (→ `C:\Hercules\GVRBoot.exe`); All Users **Startup** `LoadingShell.lnk` and `HideTaskbar.lnk` |
| 1 | `SetVolume.exe 50` | system mixer volume |
| 2 | `Config\nascarfw.exe` | runs `gvrNetworkConfig.bat`: DHCP on `local`, firewall on, 4 UDP ports opened (all stored by Windows in the registry) |
| 3 | `rundll32 NvCpl.dll,dtcfg setdvc all 20` | NVIDIA Digital Vibrance, persisted by the driver |
| 4 | `msiexec /i DbSetup\PGA.msi /quiet` | see B.5 |
| 5 | `pathman.exe /as C:\NASCAR\SHELL\BIN` | appends to machine `Path` in `HKLM\SYSTEM\...\Session Manager\Environment` |
| 6 | `DbSetup\Nascar_db.exe` | SQL schema/pricing load (database, not registry) |

### B.5 `PGA.msi` ("PGA" 1.0.0, Global VR, ProductCode `{C6BB203E-7BE8-49C0-B0C8-94BED980F3CA}`)

No `Registry` table. Its registry effects all come from three EXE custom actions:

| Custom action | When | Effect |
|---|---|---|
| `GvrPlusExportDatabaseScript.exe` | install (seq 5998) | reads `HKLM\SOFTWARE\Gvr\Plus\1.1\Cabinet\PlusSchemaPath`; builds the DB via `OSQL.exe`, rotates `sa`; on success **creates `HKLM\SOFTWARE\Gvr\Installer\DeskTopEngine\Exists = 1`**. If that key already exists the run is treated as an **upgrade**: SQL is restarted and `db_propagate.exe -recover` preserves data |
| `UnregDlls.exe` → `UnregisterDlls.bat` | install (seq 5999) | `regasm /Unregister` + `gacutil /u`, then `regasm` + `gacutil -i` on `GvrPeUtil`, `GvrPeSchemaUtil`, `GvrPeClient`, `GvrPeEngine`, `GvrPeDialer` (`C:\GvrPlus\4\lib`) — COM registration under `HKCR` + .NET 1.1 GAC |
| `DeleteRegistry.exe` | uninstall (seq 1699) | `DeleteSubKeyTree("SOFTWARE\Gvr\Installer\")` |

Plus Windows Installer's own product registration.

---

## C. What a game-only install keeps

| Keep (repointed at the install root) | Never reproduce |
|---|---|
| `GlobalVR\NASCAR` from `NASCAR.reg` + `NASCAR_Shell.reg` | `Run\GVRHercules`, `Run\HideTaskbar`, `Run\netset`, `RunOnce\CDS` |
| `gvr\Plus\2.0\Cabinet\GameRoot` | Startup `LoadingShell.lnk` / `HideTaskbar.lnk`, Start Menu `gvrStartup` / `gvrShutdown` |
| `gvr\Plus\1.1\Cabinet\*` (minus `WebServerIP`) | `Gvr\hercules\*` (`HerculesCab.reg`) |
| Anark prefs (`GvrShell.reg`) when the shell is installed | `ConfigEventLog.reg`, `tsunami.reg`, cursor scheme (opt-in only) |
| | autologon, `DisableCAD`, the `cabinet` user's 22 Explorer policies, `pathman` PATH edit |

`Gvr\Installer\DeskTopEngine` only matters if the OEM `GvrPlusExportDatabaseScript` is re-run; the
SQLite route does not use it.

## Corrections to older notes

- `NASCAR_INSTALL_ANALYSIS.md` §1 described `Shortcut.wsf` as only creating the "Close Apps"
  shortcut. It also creates `gvrStartup.lnk` and the **two Startup-folder autostarts**
  (`LoadingShell.lnk`, `HideTaskbar.lnk`).
- `NASCAR_REGISTRY.md` §4 said nothing GVR-specific survives in `cabinet\NTUSER.DAT` beyond
  MUICache. The 22 Explorer lockdown policies (A.4) are there.
