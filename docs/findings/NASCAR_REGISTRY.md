# NASCAR cabinet registry — including the values the discs never ship

Everything below was read **out of the cabinet's own registry hives** inside the recovery
image, not guessed:

```
Extracted\RecoveryImage\WINDOWS\system32\config\{SYSTEM,SOFTWARE,DEFAULT}
Extracted\RecoveryImage\Documents and Settings\cabinet\NTUSER.DAT
```

`Tools\Dump-Hive.py` reads offline `regf` hives directly, so nothing has to be `reg load`ed
(which would need admin and would mount a 2008 XP-Embedded hive on a modern machine):

```powershell
python Tools\Dump-Hive.py <hive> "Key\Path" [depth]
python Tools\Dump-Hive.py <hive> --find RUNTIMEOEM
```

---

## 1. The OEM-revision gate (the NASCAR equivalent of the NFSU value)

This is the value the OEM `Setup.exe` checks before it will install anything. NFSU's is
`NFS - UG,XP Embedded,HW Rev 865 e,05052005`; NASCAR's, read from
`HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment`:

| Name | Type | Value |
|---|---|---|
| **`RUNTIMEOEMREV`** | REG_SZ | **`NASCAR,XP Embedded,HW Rev 945-G31,08252008`** |
| **`RUNTIMEOEMVERSION`** | REG_SZ | **`1.5.0.04`** |

Present identically in `ControlSet001` and `ControlSet002`.

Same grammar as NFSU: `<Product>,XP Embedded,HW Rev <hardware revision>,<MMDDYYYY>`.
`945-G31` is the Intel 945 board / G31 chipset cabinet revision (it also appears in the
recovery disc's `GVR\GVRPESHL.INI` as `HwRev=945-G31`), and `08252008` is the image build
date — one day before the `ReleaseDate=08/26/2008` in that same INI.

### How the check is satisfied

`GVRSETUP.INI` states the minimum the installer will accept:

```ini
[MINIMUMOS]
OSName=NASCAR          ; matched against the product field of RUNTIMEOEMREV
OSDate=20070115        ; matched against the date field  (08252008 -> 2008-08-25, passes)
OSVersion=1.0.0        ; matched against RUNTIMEOEMVERSION (1.5.0.04, passes)
```

So to make the **original** `Setup.exe` run on ordinary Windows you need both values, not
just `RUNTIMEOEMREV`. The installer here can write them for you:

```powershell
.\Install-NASCAR-GVR-Portable.ps1 -SetOemRev
```

**You do not need this for our install.** `Install-NASCAR-GVR-Portable.ps1` copies the
payload directly and never runs the OEM installer, and `NASCAR_GVR.exe` itself contains no
`RUNTIMEOEMREV` string — the gate is purely an installer-side check. `-SetOemRev` exists for
the case where you deliberately want to run the OEM flow (and everything in
`NASCAR_INSTALL_ANALYSIS.md` that comes with it).

## 2. The rest of the XPe environment block

The other cabinet-identifying values in the same key, for reference:

| Name | Value |
|---|---|
| `ProductName` | `NASCAR` |
| `BuildDir` | `NASCAR` |
| `OSVERSION` | `1.5` |
| `RUNTIMESKUCODE` | `XPeCli` (XP Embedded Client) |
| `RUNTIMEGUID` | `{78860E0A-32BF-44BD-B597-E34B7FEB702D}` |
| `WEBUILDDATE` | `7/28/2006 4:29:54 PM` (base image build, predates the NASCAR layer) |
| `lib` | `C:\Program Files\SQLXML 3.0\bin\` |
| `Path` | `…;C:\Program Files\Intel\DMIX;C:\Program Files\Microsoft SQL Server\80\Tools\Binn\` |

`RUNTIMEPID` also carries the XP Embedded product key — present in the hive, deliberately
not reproduced here; read it yourself with `Dump-Hive.py` if you ever need to reinstall the
OEM OS.

Note `Path` already contains the MSDE tools directory but **not** `C:\NASCAR\SHELL\BIN` —
that entry is appended at install time by `pathman.exe` (`GVRSETUP.INI` step 5).

## 3. What the recovery image does *not* contain

The image is the OS **before the game is installed**, so these keys are simply absent:

* `HKLM\SOFTWARE\GlobalVR` (and therefore `GlobalVR\NASCAR`)
* `HKLM\SOFTWARE\Gvr` / `gvr\Plus\…`
* `HKLM\SOFTWARE\Tsunami`

They are created by the disc's `Registry\*.reg` files. So there is no "hidden" game registry
to recover — the disc `.reg` files plus the game's own runtime writes are the complete set.
What the image *does* add is the cabinet OS layer below, which the discs never ship.

## 4. Cabinet OS layer (in the image, not on the discs)

### Autologon — `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon`

| Name | Value |
|---|---|
| `AutoAdminLogon` | `1` |
| `DefaultUserName` / `AltDefaultUserName` | `cabinet` |
| `Shell` | `Explorer.exe` (the cabinet keeps Explorer and hides it, rather than replacing the shell as NFSU does) |
| `AutoRestartShell` | `1` |

(`DefaultPassword` is in the hive too if you ever need to log into a restored cabinet.)

### Autostart — `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`

| Name | Value |
|---|---|
| `netset` | `C:\GVR\GVR_Tools\netset.exe` |
| `HideTaskbar` | `C:\GVR\GVR_Tools\HideTaskBar.exe` |
| + audio vendor entries | `HDAShCut.exe`, `RTHDCPL.EXE`, `Alcmtr.exe`, `cmicnfg.cpl`, `TweakUI` |

`Startup.reg` on the game disc *adds* `GVRHercules = c:\hercules\gvrboot.exe` on top of these.
None of the three GVR entries are reproduced by our installer — see
`NASCAR_INSTALL_ANALYSIS.md`.

The rest of the image's autostart surface — `RunOnce\CDS`, `SetupExecute`, the Startup folder,
`DisableCAD`, and the non-stock auto-start services — is mapped in
`NASCAR_AUTOSTART_AND_INSTALL_REGISTRY.md` §A.

### Dongle drivers — `HKLM\SYSTEM\CurrentControlSet\Services`

This is the driver stack `Shell\bin\Dongle.dll` talks to, and it pins down exactly what the
dongle blocker (`NASCAR_FINDINGS.md` §5) involves:

| Service | Display name | Start | Image |
|---|---|---|---|
| `akshasp` | Aladdin HASP Key | 3 (demand) | `system32\DRIVERS\akshasp.sys` |
| `aksusb` | Aladdin USB Key | 3 (demand) | `system32\DRIVERS\aksusb.sys` |
| `Hardlock` | Hardlock | 2 (auto) | `system32\drivers\hardlock.sys` |
| `Haspnt` | Haspnt | 2 (auto) | `system32\drivers\Haspnt.sys` |

All five driver binaries (plus `aksclass.sys`) are present in the image, and the installer
package for them is `!Drivers\HASP\hdd32.exe`. Installing them does **not** substitute for a
physical key — but it tells us precisely which API a `Dongle.dll` replacement has to
impersonate: Aladdin HASP4 / HASP HL over `hardlock.sys` + `aksusb.sys`.

### Per-user hive (`cabinet` NTUSER.DAT)

No `Run`/`RunOnce` and no per-user `Shell` override. But the `cabinet` user **is locked down by
policy**: `Software\Microsoft\Windows\CurrentVersion\Policies\Explorer` holds 22 values
(`NoClose`, `HideClock`, `LockTaskbar`, `NoSetTaskbar`, `NoFind`, …; full list in
`NASCAR_AUTOSTART_AND_INSTALL_REGISTRY.md` §A.4) — never reproduce them. Beyond that, only
`MUICache` entries for `netset.exe` / `HideTaskBar.exe` / `fbreseal.exe`. The Anark client preferences
(`HKCU\Software\Anark\Client\3.0\Preferences`) are **not** in the image — they come from the
disc's `GvrShell.reg` at install time.

## 5. Summary: which registry values actually matter

| Value | Needed to… | Written by our installer? |
|---|---|---|
| `RUNTIMEOEMREV` + `RUNTIMEOEMVERSION` | run the **OEM** `Setup.exe` | only with `-SetOemRev` |
| `HKLM\SOFTWARE\GlobalVR\NASCAR\*` | run the game and shell (paths, resolution, launch wiring) | **yes**, repointed at your install root |
| `HKLM\SOFTWARE\gvr\Plus\2.0\Cabinet\GameRoot` | let `GvrIO.dll` find `Config\GvrIO.xml` | **yes** |
| `HKLM\SOFTWARE\gvr\Plus\1.1\Cabinet\*` | GvrPlus schema/key paths (and how the SQLite provider locates `game.db`) | **yes**, unless `-SkipSqlite` |
| `HKCU\Software\Anark\Client\3.0\Preferences` | Anark shell rendering prefs | with the shell |
| `Run\GVRHercules`, `HideTaskbar`, `netset`, Winlogon autologon | turn a PC into a cabinet | **never** |
