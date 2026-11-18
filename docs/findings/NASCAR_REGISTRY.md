# NASCAR cabinet registry — including the values the discs never ship

Everything below was read out of the cabinet's own registry hives inside the recovery image. **None of it is guessed.** The hives are:

```
Extracted\RecoveryImage\WINDOWS\system32\config\{SYSTEM,SOFTWARE,DEFAULT}
Extracted\RecoveryImage\Documents and Settings\cabinet\NTUSER.DAT
```

`Tools\Dump-Hive.py` reads offline `regf` hives directly, so nothing has to be loaded with `reg load`. That command would need admin rights. It would also mount a 2008 XP-Embedded hive on a modern machine.

```powershell
python Tools\Dump-Hive.py <hive> "Key\Path" [depth]
python Tools\Dump-Hive.py <hive> --find RUNTIMEOEM
```

---

## 1. The OEM-revision gate

The OEM `Setup.exe` checks this value before it installs anything. NASCAR's values, read from `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment`, are:

| Name | Type | Value |
|---|---|---|
| `RUNTIMEOEMREV` | REG_SZ | `NASCAR,XP Embedded,HW Rev 945-G31,08252008` |
| `RUNTIMEOEMVERSION` | REG_SZ | `1.5.0.04` |

Both are present, with the same values, in `ControlSet001` and `ControlSet002`.

The format is `<Product>,XP Embedded,HW Rev <hardware revision>,<MMDDYYYY>`. It is the same across GlobalVR's XP-Embedded cabinets.

`945-G31` is the cabinet revision with the Intel 945 board and G31 chipset. It also appears in the recovery disc's `GVR\GVRPESHL.INI` as `HwRev=945-G31`. `08252008` is the image build date. That is one day before the `ReleaseDate=08/26/2008` in the same INI.

### How the check is satisfied

`GVRSETUP.INI` states the minimum the installer accepts:

```ini
[MINIMUMOS]
OSName=NASCAR          ; matched against the product field of RUNTIMEOEMREV
OSDate=20070115        ; matched against the date field  (08252008 -> 2008-08-25, passes)
OSVersion=1.0.0        ; matched against RUNTIMEOEMVERSION (1.5.0.04, passes)
```

To make the original `Setup.exe` run on ordinary Windows, both values are needed, not just `RUNTIMEOEMREV`. The installer can write them:

```powershell
.\Install-NASCAR-GVR-Portable.ps1 -SetOemRev
```

**The portable install does not need this.** `Install-NASCAR-GVR-Portable.ps1` copies the payload directly and never runs the OEM installer. `NASCAR_GVR.exe` itself contains no `RUNTIMEOEMREV` string, so the gate is purely an installer-side check. `-SetOemRev` is only for running the OEM flow on purpose. That flow brings everything described in `NASCAR_INSTALL_ANALYSIS.md` with it.

## 2. The rest of the XPe environment block

The same key holds other values that identify the cabinet. They are listed for reference:

| Name | Value |
|---|---|
| `ProductName` | `NASCAR` |
| `BuildDir` | `NASCAR` |
| `OSVERSION` | `1.5` |
| `RUNTIMESKUCODE` | `XPeCli` (XP Embedded Client) |
| `RUNTIMEGUID` | `{78860E0A-32BF-44BD-B597-E34B7FEB702D}` |
| `WEBUILDDATE` | `7/28/2006 4:29:54 PM` (base image build, older than the NASCAR layer) |
| `lib` | `C:\Program Files\SQLXML 3.0\bin\` |
| `Path` | `…;C:\Program Files\Intel\DMIX;C:\Program Files\Microsoft SQL Server\80\Tools\Binn\` |

`RUNTIMEPID` also holds the XP Embedded product key. It is in the hive but is left out of this document on purpose. Read it with `Dump-Hive.py` if the OEM OS ever has to be reinstalled.

`Path` already contains the MSDE tools folder, but not `C:\NASCAR\SHELL\BIN`. `pathman.exe` adds that entry at install time (`GVRSETUP.INI` step 5).

## 3. What the recovery image does *not* contain

The image is the OS before the game is installed, so these keys are absent:

* `HKLM\SOFTWARE\GlobalVR` (and therefore `GlobalVR\NASCAR`)
* `HKLM\SOFTWARE\Gvr` / `gvr\Plus\…`
* `HKLM\SOFTWARE\Tsunami`

The disc's `Registry\*.reg` files create them. So there is no hidden game registry to recover. The disc `.reg` files plus the game's own runtime writes are the complete set.

What the image does add is the cabinet OS layer in the next section. The discs never ship it.

## 4. Cabinet OS layer (in the image, not on the discs)

### Autologon — `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon`

| Name | Value |
|---|---|
| `AutoAdminLogon` | `1` |
| `DefaultUserName` / `AltDefaultUserName` | `cabinet` |
| `Shell` | `Explorer.exe`. The cabinet keeps Explorer and hides it instead of replacing the shell. |
| `AutoRestartShell` | `1` |

`DefaultPassword` is in the hive too, for logging into a restored cabinet.

### Autostart — `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`

| Name | Value |
|---|---|
| `netset` | `C:\GVR\GVR_Tools\netset.exe` |
| `HideTaskbar` | `C:\GVR\GVR_Tools\HideTaskBar.exe` |
| + audio vendor entries | `HDAShCut.exe`, `RTHDCPL.EXE`, `Alcmtr.exe`, `cmicnfg.cpl`, `TweakUI` |

`Startup.reg` on the game disc adds `GVRHercules = c:\hercules\gvrboot.exe` on top of these. The installer reproduces none of the three GVR entries. See `NASCAR_INSTALL_ANALYSIS.md`.

`NASCAR_AUTOSTART_AND_INSTALL_REGISTRY.md` §A maps the rest of the image's autostart entries. These are `RunOnce\CDS`, `SetupExecute`, the Startup folder, `DisableCAD` and the non-stock auto-start services.

### Dongle drivers — `HKLM\SYSTEM\CurrentControlSet\Services`

`Shell\bin\Dongle.dll` talks to this driver stack. It shows exactly what the dongle blocker (`NASCAR_FINDINGS.md` §5) involves.

| Service | Display name | Start | Image |
|---|---|---|---|
| `akshasp` | Aladdin HASP Key | 3 (demand) | `system32\DRIVERS\akshasp.sys` |
| `aksusb` | Aladdin USB Key | 3 (demand) | `system32\DRIVERS\aksusb.sys` |
| `Hardlock` | Hardlock | 2 (auto) | `system32\drivers\hardlock.sys` |
| `Haspnt` | Haspnt | 2 (auto) | `system32\drivers\Haspnt.sys` |

All five driver binaries (plus `aksclass.sys`) are present in the image. Their installer package is `!Drivers\HASP\hdd32.exe`.

Installing these drivers does **not** replace a physical key. They do show which API a `Dongle.dll` replacement has to imitate: Aladdin HASP4 / HASP HL over `hardlock.sys` + `aksusb.sys`.

### Per-user hive (`cabinet` NTUSER.DAT)

The hive has no `Run`/`RunOnce` entries and no per-user `Shell` override.

The `cabinet` user **is locked down by policy**, though. `Software\Microsoft\Windows\CurrentVersion\Policies\Explorer` holds 22 values, such as `NoClose`, `HideClock`, `LockTaskbar`, `NoSetTaskbar` and `NoFind`. The full list is in `NASCAR_AUTOSTART_AND_INSTALL_REGISTRY.md` §A.4. Never reproduce these values.

Beyond that, the hive holds only `MUICache` entries for `netset.exe` / `HideTaskBar.exe` / `fbreseal.exe`.

The Anark client preferences (`HKCU\Software\Anark\Client\3.0\Preferences`) are not in the image. They come from the disc's `GvrShell.reg` at install time.

## 5. Summary: which registry values actually matter

| Value | Needed to… | Written by the installer? |
|---|---|---|
| `RUNTIMEOEMREV` + `RUNTIMEOEMVERSION` | run the OEM `Setup.exe` | only with `-SetOemRev` |
| `HKLM\SOFTWARE\GlobalVR\NASCAR\*` | run the game and shell: paths, resolution, launch wiring | yes, pointed at the install root |
| `HKLM\SOFTWARE\gvr\Plus\2.0\Cabinet\GameRoot` | let `GvrIO.dll` find `Config\GvrIO.xml` | yes |
| `HKLM\SOFTWARE\gvr\Plus\1.1\Cabinet\*` | GvrPlus schema and key paths, also how the SQLite provider finds `game.db` | yes, unless `-SkipSqlite` |
| `HKCU\Software\Anark\Client\3.0\Preferences` | Anark shell rendering preferences | with the shell |
| `Run\GVRHercules`, `HideTaskbar`, `netset`, Winlogon autologon | turn a PC into a cabinet | **never** |
