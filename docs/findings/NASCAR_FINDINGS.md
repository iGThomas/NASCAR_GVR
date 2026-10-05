# NASCAR Team Racing (GlobalVR) — reverse-engineering findings

Companion to the parent project's `REVERSE_ENGINEERING_FINDINGS.md`, but for the NASCAR
cabinet. Everything below was verified against the extracted media on 2026-08-12 unless
marked *(inferred)*.

---

## 1. What the media is

### Game disc — `GAME ISO EXTRACTED\`

InstallShield 12 OEM installer.

| Item | Value |
|---|---|
| `Setup.ini` AppName | `NASCAR` |
| ProductGUID | `70AA52E7-89A2-428E-8C92-3ABC73B83F04` |
| Payload | `data1.hdr` + `data1.cab` + `data2.cab` (646 MB) |
| Extracted | 4,914 files, ~800 MB |
| Extractor | the project's `release\Tools\unshield.exe` handles it directly |
| Author string in `setup.inx` | `C:\Projects\NASCAR\SETUP\Installer\Script Files\Setup.dbg` |

`setup.inx` still contains the `RUNTIMEOEMREV` string — the same XP-Embedded gate the NFSU
discs use, so the OEM installer refuses to run on ordinary Windows. **The game executable
itself does not check it** (no such string in `NASCAR_GVR.exe`), which is why a
file-copy installer works.

NASCAR's value, read out of the cabinet's own SYSTEM hive in the recovery image:

```
RUNTIMEOEMREV     = NASCAR,XP Embedded,HW Rev 945-G31,08252008
RUNTIMEOEMVERSION = 1.5.0.04
```

Both are needed to satisfy the gate (`GVRSETUP.INI [MINIMUMOS]` checks the product name,
the date and the version). The installer writes them with `-SetOemRev`. Full registry
reference, including the cabinet OS layer the discs never ship:
**[`NASCAR_REGISTRY.md`](NASCAR_REGISTRY.md)**.

### Recovery disc — `RECOVERY DISC ISO EXTRACTED\`

A bootable WinPE 2.0 restore disc, **not** a game disc.

* `BOOTMGR` + `BOOT\BCD` + `SOURCES\BOOT.WIM` (89 MB) — the WinPE that runs at boot.
* `GVR\GVRPESHL.INI` — the restore job description:
  ```ini
  [Global VR SRD]
  Product=NASCAR
  HwRev=945-G31          ; Intel 945 board / G31 chipset cabinet revision
  ReleaseDate=08/26/2008
  Volume=C
  VolumeLabel=NASCAR
  SettleSeconds=45
  ```
* `GVR\TOOLS\DISKPART.TXT` — **destructive**: `select disk 0` / `clean` / create+format C:.
* `GVR\NASCAR-20080826-1.GVR` (425 MB) — despite the extension this is a plain
  **Microsoft WIM** (`MSWIM` magic, format version 1.13). 7-Zip, `wimlib`, and DISM all
  read it.

WIM metadata: image name `NASCAR-20080826-1`, `PRODUCTSUITE=EmbeddedNT`,
Windows 5.1.2600 SP build 2180 (XP Embedded SP2), 820 dirs / 4,684 files / 1.03 GB.

Contents = the cabinet's C: drive **before the game is installed**:

| Path | What |
|---|---|
| `!Drivers\945`, `G31`, `LAN`, `LAN8139`, `NVIDIA\177.83`, `NVIDIA\94.24` | board/GPU/NIC drivers |
| `!Drivers\HASP\hdd32.exe` | Aladdin HASP device driver (4.4 MB) — the dongle driver |
| `WINDOWS\Microsoft.NET\Framework\v1.0.3705`, `v1.1.4322` | .NET, installed state |
| `Program Files\Microsoft SQL Server\MSSQL` | MSDE 2000 SP3 (`sqlservr.exe` 8.00.760), **system DBs only** |
| `Program Files\SQLXML 3.0` | required by the GvrPlus stack |
| `Program Files\DivX`, `C-Media`, `Realtek`, `Intel` | codec + audio + NIC packages |
| `GVR\GVR_Tools` | `SetVolume`, `SetVRes`, `HideTaskBar`, `ShowOemRev`, `CDS`, `fbreseal`, `finishos`, … |
| `GVR\DIAG` | `SimpleHIDWrite.exe`, Nytric `MultiUSBIOTest.exe` — I/O board diagnostics |

So the answer to "the prerequisites are inside the .GVR" is: yes, but as **installed
state**, not as redistributable installers. The only genuine installers inside are device
drivers (NVIDIA, HASP, Intel LAN, C-Media).

---

## 2. Payload → install layout

The **cabinet's** layout is unambiguous — it is pinned by the OEM registry files and
`GVRSETUP.INI`, not guessed. (Our installer does **not** reproduce these paths: it nests
everything under a folder you choose and repoints the registry accordingly — see the
README. The table below is what the OEM installer would have done.)

| Cabinet file group | Destination | Evidence |
|---|---|---|
| `NASCAR\Game` | `C:\NASCAR\Game` | `GVRSETUP.INI [GAMEINSTALLED] File=C:\NASCAR\Game\NASCAR_GVR.exe`; `NASCAR_Shell.reg launchFolder` |
| `NASCAR\Shell` | `C:\NASCAR\Shell` | `NASCAR_Shell.reg attractFolder/selectFolder`; `HerculesCab.reg Prog04 path` |
| `Hercules` | `C:\hercules` | `HerculesCab.reg` `path="c:\\hercules"` |
| `GVRPLUS` | `C:\GvrPlus` | `Schema.reg` `C:\GvrPlus\4\...`; `GVRSETUP.INI` DB step working dir |
| `GVR\GVR_Tools` | `C:\GVR\GVR_Tools` | `GVRSETUP.INI` `C:\GVR\GVR_Tools\SetVolume.exe` |
| `WINDOWS\system32`, `Fonts`, `Cursors` | `%SystemRoot%\…` | file group naming + `NASCAR.reg` cursor paths |

### Game tree size breakdown

```
Audio      3,193 files  357.6 MB      Options       73 files    7.7 MB
GameData     913 files  189.9 MB      Telemetry     57 files    9.7 MB
GameSpy        2 files    6.2 MB      Save          25 files    0.1 MB
Replays        1 file     5.3 MB      Animations     4 files    1.2 MB
```

---

## 3. The game binary

`NASCAR_GVR.exe` — 3.6 MB, PE32, built from `\Projects\Nascar\Dev\Source\Pc\…`.

**This is EA's NASCAR engine** wrapped in a GVR arcade shim: it loads `.MAS` archives
(`EALoad.mas`, `SpecialFX.mas`), `.gdb` season databases, `.sci`/`.tbc`/`.svm` sim files,
Miles Sound System (`Mss32.dll`) and Bink (`binkw32.dll`) — the NASCAR Thunder /
SimRacing lineage. Retail-engine knowledge (PLR/ini tuning files, `.mas` tooling)
transfers directly.

### Imports (verified)

```
NASCAR_GVR.exe : WINMM, DSOUND, KERNEL32, USER32, GDI32, ADVAPI32, SHELL32,
                 binkw32, d3d9, DINPUT8, mss32, GvrIO, MSVCP71, WS2_32
AMPlayer.exe   : OPENGL32, GLU32, DDRAW, MSACM32, WININET, urlmon, RPCRT4,
                 MSVCR71, MSVCP71, COMCTL32, …          (Anark Media Player 3.0)
GvrIO.dll      : USBIOExtreme, DINPUT8, HID, SETUPAPI, ole32, OLEAUT32, MSVCR71
motionLib.dll  : cbw32 (Measurement Computing InstaCal — motion base only)
```

**Key result: the game executable has no SQL, no .NET, no PLUSDE, no GvrPlus dependency.**
The database exists for the shell's accounting/leaderboards. A game-only install therefore
does not need MSDE at all — a much shorter path than NFSU took.

### Hardcoded paths

Only one: `C:\gvrtime.txt`. Everything else is relative to the working directory
(`Config.ini [COMPONENTS]`) or comes from the registry.

### Command line (from the arg table in the binary)

```
-mode single|career          -track <NAME>        -series <NAME>
-car <n>                     -laps <n>            -raceLength <n>
-startPos <n>                -playernumber <n>    -playercount <n>
-server | -client <ip>       -netdbg lo|med|hi    -notimeout  -noabort
-windowed | -fullscreen      -width <n> -height <n>
-lowgraphics -nopsys -nodebris -noshake -showtabs -hud <0..3>
-nomusic -nochatter -noengine -nopengine -noaiengine -nosoundfx
-boosty <f> -handling -dassist -spacing -qspacing -doubleline -aiclump
-transManual -ff_fxdisable -noautoinput -cmdfile <file> -tracefilenum <n>
-time <n> -password "…"      +heapmegs=<n>
(dev/test: -wsmoketest -bsmoketest -flametest -crashtest)
```

`-cmdfile` reads arguments from a text file; the shipped `Game\cmdline.txt` is an
example of that format (`//` comments) left over from development.

### Content actually shipped

Tracks: `BRISTOL`, `DAYTONA`, `INDIANAPOLIS`, `LOWES`, `PHOENIX`, `TALLADEGA`
(+ `Common` and `HAT` support folders). The binary knows 24 track names — the rest are
retail-engine leftovers with no data on the disc.

Series: `2006NEXTEL`, `2005NEXTEL`, `2004NEXTEL`, `2004NNS`, `2004CTS`.

---

## 4. GvrIO — the hardware layer (and the plug-in ABI)

`GvrIO.dll` is the NASCAR-era equivalent of NFSU's `GVRInputRaw.dll`, but cleaner: an
11-export C++ class plus a **plug-in loader**.

```
?Initialize@cGvrIO@GvrIO@Zeus@Gvr@@…(GVR_DEVICE_TYPES, HWND, callback)
?MessageRegisterCallback@cGvrIO@…(GVR_DEVICE_TYPES, callback)
?GvrIOMessageSend@GvrIO@Zeus@Gvr@@YAPAXPAUsMessage@123@@Z
??0/??1/??4 cGvrIO, GvrIOLogger  (ctor/dtor/assign, LogPrintf, LogErrorPrintf)
```

Behaviour recovered from its strings:

1. Reads `GameRoot` from `HKLM\SOFTWARE\Gvr\Plus\2.0\Cabinet`.
2. Loads `file://<GameRoot>\Config\GvrIO.xml` (button/key map, ships in `Game\config\`).
3. Loads named plug-ins as `<name>.dll` and resolves **`Initialize`**, **`MessageCallback`**
   (and `Finalize`). Known names in the string table: `Dongle`, `CardDispenser`.
4. Device classes: `Steering`, `Joystick`, `Trackball`, `Nytric`, `GvrIOmini`, `ComboButton`.
5. Talks to the Nytric USB I/O board through `USBIOExtreme.dll`
   (`CUSBIO_Init/Update/Close`, `SetInputMode`, `SetOutputValue`) and to HID devices via
   `SETUPAPI`/`VID_%04hx&PID_%04hx&MI_%02d`.

The shipped plug-ins in `Shell\bin` that follow this ABI:
`Dongle.dll`, `Steering.dll`, `Motion.dll` — each exporting exactly
`Initialize`, `MessageCallback`, `Finalize`.

### Keyboard fallbacks (from `Game\config\GvrIO.xml`)

| Key | Function | Key | Function |
|---|---|---|---|
| `s` | Start | `q` | Quit |
| `l` | Look back | `v` | View / camera |
| `m` | Music | `o` | Operator menu |
| `t` `y` `u` `i` | Shifter 1–4 | `r` `c` `d` `g` | Forward / Back / Left / Right (menu nav) |
| `a` | Motion disable | `x` | "XKey" (test) |

---

## 5. The dongle blocker

`NASCAR_GVR.exe` contains:

```
 DONGLE found: game=%s, version=%s, region=%s, cab=%s
**  FATAL  ** INVALID DONGLE DETECTED!
**  FATAL  ** NO DONGLE DETECTED!
** Warning! *** '%s' is not a valid dongle cabinet type!
** Warning! *** '%s' is not a valid dongle region code!
StatusDongleError
```

So the game reads four fields off the key — game name, version, region code, cabinet type —
and treats absence or mismatch as fatal. There is **no `-nodongle` switch** in the arg table.

`Shell\bin\Dongle.dll` is the reader. It statically imports only `KERNEL32` + `MSVCR71` and
resolves everything else at runtime — its string table names `HASPUT16.DLL`, `wtsapi32/wfapi`
(Terminal Server detection), `UTRegister/UTUnRegister`, and
`System\CurrentControlSet\Control\ProductOptions`. That is the classic **Aladdin HASP**
runtime pattern, matching `!Drivers\HASP\hdd32.exe` in the recovery image.

**Attack point (not yet implemented):** replace `Dongle.dll` with a shim exporting
`Initialize` / `MessageCallback` / `Finalize` that answers the dongle query with
`game=NASCAR`, a valid version, region and cabinet type. This is the same shape as the
NFSU `GVRInputRaw.dll` replacement, and the NFSU lesson applies directly: **forward the
whole ABI to the original DLL renamed `Dongle_oem.dll` and only override the calls you
mean to override** — stubbing everything is what produced the long idle hangs there.

What is still unknown: the `sMessage` layout and the message IDs used for the dongle
query. That needs a Ghidra pass on `NASCAR_GVR.exe` + `Dongle.dll`.

### Observed startup behaviour (2026-08-12, Win11 x64 host, no registry keys, not elevated)

Launched `NASCAR_GVR.exe -mode single -track DAYTONA -windowed -width 800 -height 600`
from the extracted tree with the GvrIO + MSVC 7.1 DLLs staged beside it:

* process starts, 7 threads, ~15 MB working set, **stays alive past 120 s**
* creates `LOG\LOADTIME.txt` (0 bytes) — so `main()` runs and the engine's logging opens
* **never creates a window**, never writes any other file
* main thread parks in `ExecutionDelay` (a `Sleep` retry loop)

That is a *probe/retry stall*, not a crash — the same signature as the NFSU
UniverShell2 hang that turned out to be cabinet hardware probes. Ranked suspects:
1. `Dongle.dll` retrying the HASP key (no driver, no key present),
2. `GvrIO` failing to resolve `GameRoot` (registry keys were absent in this test — the
   installer writes them, so this needs re-testing after a real elevated install),
3. the Nytric/`USBIOExtreme` board enumeration.

Re-test order is in `NASCAR_PLAN.md`.

---

## 5b. Controllers — the retail EA input stack is intact

> **Corrected 2026-10-04 (§14):** on this GVR build the race opens **no DirectInput joystick**
> — only a keyboard — so the `.CTL` "controller 1" lines below are never read. The cabinet's
> wheel and pedals reach the race as GvrIO msg 5 axes and its buttons as GvrIO events; that is
> also where the Xbox / PlayStation pad support plugs in. The `.CTL` keyboard lines still apply.

Unlike NFSU (where analog steering needed a `GVRInputRaw.dll` replacement), NASCAR keeps
EA's **data-driven** controls system. ~~Driving input goes through the game's own
`DINPUT8.dll` usage, not through GvrIO~~ — GvrIO carries the cabinet *buttons*
(start/view/music/shifter/coin) **and** the analog wheel and pedal axes (§14.1).

Config files, plain text, in `Game\Save\GvrSinglePlayer\`:

| File | Contents |
|---|---|
| `GvrSinglePlayer.CTL` (22 KB) | `Player Controls File … Copyright (c) 2003-2004 Electronic Arts Inc.` — every binding, dead zone, sensitivity and FFB parameter |
| `GvrSinglePlayer.PLR` (28 KB) | player/driving prefs (`Speed Sensitive Steering`, `Opposite Lock`, cockpit options, …) |
| `GvrSinglePlayer.gal`, `tempGarage.svm`, `Settings\<series>\<track>\*.svm` | garage setups per track |

Binding syntax is `"(device, control)"`:

* device `0` = keyboard, control = **DirectInput scan code** (e.g. `200` = `DIK_UP`,
  `208` = `DIK_DOWN`, `16` = `DIK_Q`)
* device `1`+ = a DirectInput game device; control = axis or button index

Shipped cabinet mapping:

```
Control - Steer Left ="(1, 2)"    Control - Accelerate="(1, 4)"
Control - Steer Right="(1, 1)"    Control - Brake     ="(1, 3)"
Control - Shift Up   ="(1, 20)"   Control - Shift Down="(1, 19)"
```

Relevant knobs in `[ Controls ]`:

```
Basic Control Setups="1"
Force Feedback="0"                 // 0 = off, 1 = on
FFB Device Type="1"                // 0=none 1=wheel 2=joystick 3=rumble pad
Gear Select Button Hold="0"        // for shifters that hold a button per gear
Keyboard Steering/Throttle/Brake/Clutch="…"   // keyboard ramp rates
```
plus per-axis `Dead Zone` / `Sensitivity` / `Center` entries and a full FFB block
(`FFB Gain`, `FFB Effects Level`, spring coefficient/saturation per joystick axis).

**Consequence: gamepad/wheel support here is expected to be a configuration exercise, not
a DLL shim** — rebind to the pad's axes and set `FFB Device Type="3"` for a rumble pad.
This is unverified only because the dongle stall (§5) prevents reaching a race. If it
turns out the GVR shim overrides the CTL, the fallback is `Shell\bin\Steering.dll`, which
is a GvrIO plug-in with the same `Initialize`/`MessageCallback`/`Finalize` ABI as
`Dongle.dll` and can be replaced the same way.

## 5c. The database — SQLite, built and verified

`NASCAR_GVR.exe` itself has no SQL/.NET/PLUSDE dependency (§3), so **racing needs no
database at all**. The database exists for the **Anark shell** and its `GvrPlusDEPlugin`
accounting/leaderboard path — i.e. for the full cabinet experience.

That is now implemented on SQLite, so no MSDE / SQL Server is ever installed:
`PLUSDE.dll`'s 10 `System.Data.SqlClient` typerefs are swapped to a `GvrSqlite` provider
that P/Invokes `sqlite3.dll`, and `game.db` is generated from the decrypted OEM schema
(106 tables, 366 seed rows, operator pricing, 360 leaderboard rows).

Build it with `Tools\Build-SqliteBackend.ps1`, install with
`Install-NASCAR-GVR-Portable.ps1`. Full design, provisioning rationale,
verification results and known boundaries: **[`NASCAR_SQLITE.md`](NASCAR_SQLITE.md)**.

Two NASCAR-specific notes worth carrying:

* GlobalVR **reused the GvrPlus AES key/IV across titles**, so the NFSU `.enc` decryption
  works unchanged on NASCAR's 2008 media.
* The NFSU *artifacts* do not transfer — NASCAR ships a different `PLUSDE.dll`
  (1,810,432 B vs 1,789,952 B) and calls four members NFSU's build never did
  (`ExecuteScalar`, `CommandTimeout`, `SqlError.Message`, `SqlException.Message`). Only the
  technique ports.

The alternative route, if you ever want the genuine article: MSDE 2000 (or SQL Express with
the connection string in `NASCARcabinetXml.enc` re-encrypted, the way
`Repoint-GvrConnectionString.ps1` does for NFSU) + `PGA.msi` + `Nascar_db.exe`.

## 6. Registry map

The disc ships nine `.reg` files. What each one does, and whether the game-only installer
touches it:

| File | Contents | Used? |
|---|---|---|
| `NASCAR.reg` | `HKLM\SOFTWARE\GlobalVR\NASCAR` version/build/`FullScreenWidth=1360`/`Height=768`, **plus a full HKCU cursor-scheme replacement** | **Partly** — game keys yes, cursor block only with `-InstallCabinetCursors` |
| `NASCAR_Shell.reg` | launch wiring: `launchName`/`launchFolder`/`attractName`/`selectName`/`cmd="-startPos 43 "`/`MusicVolume` | **Yes**, repointed at the chosen install root |
| `NASCAR_GvrIO.reg` | `HKLM\SOFTWARE\gvr\Plus\2.0\Cabinet\GameRoot` | **Yes** |
| `GvrShell.reg` | HKCU Anark Client 3.0 prefs (`DisableGL=1`) | **Yes**, only when the shell is installed |
| `Schema.reg` | GvrPlus 1.1 cabinet paths, `WebServerIP=192.168.1.39` | Only with `-InstallGvrPlus` (and the dev web server IP is dropped) |
| `Startup.reg` | `Run\GVRHercules = c:\hercules\gvrboot.exe` | **Never** |
| `HerculesCab.reg` | coin/dongle/stall/crash monitor stack + AMPlayer autostart | **Never** |
| `ConfigEventLog.reg` | zeroes event-log retention on all three logs | **Never** |
| `tsunami.reg` | Tsunami motion base (`motionLib.dll`, `MotionType=4`) | **Never** — needs `cbw32.dll`/InstaCal hardware |

Note `FullScreenWidth=1360`/`FullScreenHeight=768`: unlike NFSU (where resolution is
hardcoded in the EXE and needed byte patches), **NASCAR takes its resolution from the
registry and the command line**. No patching required for widescreen.

On x64 Windows the 32-bit game reads `HKLM\SOFTWARE\WOW6432Node\…`; the installer writes
both views, the same lesson as `win11-x64-support` in the NFSU work.

The cabinet-OS half of the registry — the OEM-revision gate, autologon, the GVR autostart
entries and the Aladdin HASP driver services — is documented separately in
[`NASCAR_REGISTRY.md`](NASCAR_REGISTRY.md), read directly from the recovery image's hives
with `Tools\Dump-Hive.py`.

---

## 7. The shell and the database (context, not required for the game)

* Shell = **Anark Media Player** (`AMPlayer.exe` + `AKPlu*.dll` plug-ins) running
  `Shell\shell.am`, `NASCAR_Attract.am`, `NASCAR_Selection.am` — a completely different
  technology from NFSU's UniverShell2, so none of the `.gvr`/GVRD/script-VM work from the
  parent project applies here.
* `Game\config\GvrShellPlugInList.xml` lists the shell's plug-ins: `ZeusIOPlugIn`,
  `HerculesPlugIn`, `GvrDiagnosticsPlugIn`, `NASCARPlugIn`, `GvrMusicPlugIn`,
  `GvrDirtyWordPlugIn`, `GvrPlusDEPlugin`, `GvrLinkingPlugIn`, `GvrMotionPlugIn`.
* DB install on the cabinet = `msiexec /i DbSetup\PGA.msi /quiet` (a VS.NET setup project,
  .NET 1.1 assembly) followed by `DbSetup\Nascar_db.exe` with working directory
  `C:\GvrPlus\4\schema\game`, which loads the `.tbl`/`.sql` files shipped there
  (`a+PricingConfiguration.tbl`, `NascarLeaderboard.sql`, `adminplus.sql`, …).
* The encrypted connection string lives at `C:\GvrPlus\4\schema\NASCARcabinetXml.enc` —
  the direct analogue of NFSU's `nfscabinetXml.enc`, which we know is AES-128 and
  re-encryptable (see `db-connstring-reencryptable` findings in the parent project).

### Relationship to the NFSU stack

Shared DLL *names*, different builds — every common file differs byte-for-byte from the
NFSU copies (NASCAR is 2008, NFSU is 2005):

| DLL | NASCAR | NFSU | Same file? |
|---|---|---|---|
| `PLUSDE.dll` | 1,810,432 B | 1,789,952 B | no |
| `GVRStorageDevice.dll` / `DongleStorageDevice.dll` / `GVRSDEmulator.dll` / `GVRSCR28.dll` / `PCSCSCR2.dll` | present | present | no |
| `GVRInputRaw.dll` | 106,496 B | 53,248 B | no |

So the **techniques** port (PLUSDE→SQLite shim, ABI forwarding, minimal registry), but not
the patched binaries — anything derived from NFSU's `PLUSDE.dll` must be re-derived here.

NASCAR-only additions: `akshasp.dll`, `haspms32.dll` (HASP), `MFC71.dll`, `msvcr70.dll`,
`UsbTrackerDll.dll`, `MdmXSdk.dll`, `csamsp.dll`, `GVR_Resources.dll`, `PATHMAN.EXE`.

---

## 8. Startup, solved (2026-09-30) — the three things that stopped the game

Verified on Win11 x64 with x32dbg (MCP, port 3032) + Ghidra (EXE base 0x400000 == live base,
so every address here maps 1:1 in both tools).

### 8.1 Read the game's own log first

`main()` calls `FUN_004430e0()` before anything else, and that opens **`Game\LOG\trace00N.txt`**
(N increments per run; `LOGDIR=LOG\` in `Config.ini`). The trace contains:

* the parsed command line (`ArgManager: CmdLine=<...>`, then one line per recognised arg)
* the dongle result (`DONGLE found: game=%s, version=%s, region=%s, cab=%s`)
* state banners `[ ENTER ] [ SETUP ] [ INIT ] [ RESTART ] [ POST ]` and the matching
  `**FATAL** Game failed the <X> state`
* `LOADTIME:` timings per init step, `== SystemInfo ==`, the chosen video mode
* every `SetError FATAL! : 'Could not locate ...'`
* `*** BADSTUFF: Unhandled Exception! ***` with ExpCode / ExpFlags / ExpAddress
* `~ fini ~` on a clean exit

It only *looked* like the game was silent: **the OEM tree ships no `LOG` folder and the engine
will not create one**, so every write failed. `Install-Payload` now creates it. This is the
single most useful diagnostic in the project — check it before reaching for a debugger.
`OutputDebugString` is not a useful channel: the only message the game ever emits is
`ACTIVATE` (seen as DBG_PRINTEXCEPTION_C 0x40010006 under a debugger, or via DBWIN otherwise).

### 8.2 The resource manager's directory table, and a NULL-deref bug

`DAT_0078E6E8` is the first manager built by `Game::CreateManagers` (`FUN_00447790`,
allocation 0x25C, constructor `FUN_00486210`). It holds a table of directory names as
{char* ptr, int len, ...} quads:

| offset | value |
|--------|-------|
| +0x1C0 | `CreateACar\` |
| +0x1D0 | `__CreateACar\` |
| +0x1E0 | `Basic Settings\` |
| +0x1F0 | `UI Elements\` |
| +0x200 | `UI Elements\` |
| +0x210 | empty string |

The code at 0x0048B8E0 builds the directory plus `*.CTL` and enumerates it (`FUN_004F9710`,
then `FUN_004F9BE0` for the first match):

```
0048B8EE  mov  ecx,[0078E6E8]      ; the resource manager
0048B8F4  mov  eax,[ecx+1E0]       ; -> "Basic Settings\"
0048B908  call 004FC1C0            ; build the search path
0048B91C  call 006BA229            ; sprintf -> "*.CTL"
0048B941  call 004F9710            ; enumerate <dir>*.CTL
0048B95D  call 004F9BE0            ; first match -> eax
0048B968  jz   0048B97A            ; empty list: SKIP the walk...
0048B97D  mov  [esi+08],eax        ; ...but still store NULL as "current"
0048B984  call 004F9A70            ; resolve its path -> NULL (entry+8 == 0)
0048B990  mov  cl,[eax]            ; *** AV: strcpy from NULL ***
```

**If the enumeration is empty the engine still stores NULL as the current entry, calls the
path resolver anyway, and copies from the NULL it returns.** The resolver:

```c
char* FUN_004f9a70(void)            /* object passed in EAX */
{
    piVar3 = *(int**)(*in_EAX + 8); /* the current entry */
    if (piVar3 == 0 || *piVar3 == 0) return 0;   /* <-- taken; returns NULL */
    ...
    sprintf((char*)(*in_EAX + 0x10), "%s%s", basedir /* entry+0x144 */, entry + 0x30);
```

So one missing or misnamed directory is a silent, invisible startup crash: the
unhandled-exception filter `FUN_0045dc10` logs to the trace and `_exit(-1)`s, producing no
dialog and no WER event.

**Names the engine requires with spaces** — authoritative, from the cabinet's own directory
table via `unshield l data1.cab`, not guessed:

    2004 NNS   2005 NEXTEL Cup   Basic Settings   New Hampshire
    Pit Elements   Tab Backgrounds   UI Elements

**`unshield` rewrites DIRECTORY names when extracting** (spaces become underscores) while
leaving FILE names untouched; `-R` disables the conversion. Repair with
`Tools\Fix-GvrDirNames.ps1`. Never blanket-replace underscores, because `END_OF_SEASON`,
`MAIN_MENU`, `PP_SOT`, `Career_NEXTEL` and friends are genuine. Validation: the repaired tree
matches 4780 of 4791 cabinet entries, the other 11 being `InstallGVR\*`, which is not deployed.

Other strings in the EXE that confirm space-bearing names:
`Options\Preview Images`, `GAMEDATA\VEHICLES\2004 NEXTEL CUP\FORD\17\17.VEH`,
`%s%s%s\%s Load%.2d.jpg`, `%s%s%s\%s SyncScreen.jpg`.

### 8.3 The HASP dongle gate — complete map

HASP is **statically linked** into `NASCAR_GVR.exe`, so there is no DLL to replace.
Record: 27 bytes at `0x00779CC0 + idx*0x1b`; `+0` present flag, `+1` game name (NUL at +7).
`DAT_0077A290` is the index bound — 0 when nothing is attached, which still admits index 0.

```
FUN_00672460(idx)   zero the 27-byte record, FUN_00672280() fills it from HASP
                    services 1/5/0x32; on success record[0]=1, return 1     [83 bytes]
FUN_006721d0()      -> FUN_00672460(0)          "dongle present"
FUN_006721f0()      -> FUN_00672460(0)          "dongle valid"
FUN_006724c0(0)     -> record[0]
FUN_006724f0(0)     -> &record[1]
FUN_00672220()      -> FUN_006724c0(0) ? FUN_006724f0(0) : 0
FUN_00672200/240/260()  version / region / cabinet-type accessors
```

`main()` (`FUN_0045dc80`) keeps a local `bVar1 = true` and only clears it when
`present && valid && stricmp(FUN_00672220(), "NASCAR") == 0`. While set, the whole
`INIT` then `RESTART` then play sequence is skipped, so the game shuts down having logged
only `**  FATAL  ** NO DONGLE DETECTED!`.

A **39-byte stub** over `FUN_00672460` satisfies all three checks (see
`src\GvrIOShim\GvrIOShim.cpp :: patch_dongle_gate`): write 1 to `record[0]`, `NASCAR` plus a
NUL to `record[1..7]`, return 1. The real function is 83 bytes
(0x00672460..0x006724B2, then `CC` padding), so nothing after it is disturbed.

It is applied from the shim's DllMain, which runs before `main()`, so the executable is never
modified on disk and every launcher behaves identically. Guard rails: the original 16 bytes
(`8B 54 24 04 85 D2 7C 48 3B 15 90 A2 77 00 7F 40`) are verified first and a mismatch refuses
the patch, the record address is computed from the real load base, and
`GVRIOSHIM_NO_DONGLE_PATCH=1` disables it for a cabinet with a genuine dongle.

Result in the trace: `DONGLE found: game=NASCAR, version=, region=, cab=`. Version, region and
cabinet type stay empty, which logs two non-fatal warnings (`game.cpp` 3582 / 3603).

### 8.4 main()'s startup state machine

```
FUN_004430e0()                  open LOG\trace00N.txt
SetUnhandledExceptionFilter(FUN_0045dc10)
FUN_0045d240(argv)              ArgManager
FUN_00447790()                  Game::CreateManagers  -> _exit(0) on failure
FUN_004880e0(DAT_0078e6e8)      "Unable to configure the game" on failure
FUN_00694850("NASCAR_GVR") / FUN_006946c0(45000)    stall monitor, 45 s
  <dongle gate>
FUN_00448960()                  ENTER    -> Game::GvrEnter, needs a track
FUN_00448d40()                  SETUP
FUN_00449130()                  INIT     (skipped unless the dongle passed)
FUN_00449a80()                  RESTART
FUN_0044aa00()                  play
FUN_0044aff0 / FUN_0044b240 / FUN_00447c80    POST / teardown, then "~ fini ~"
```

Switches parsed with `FUN_004fcdf0`: `+heapmegs=N`, `+allowmultiple`, `+altosc`,
`+noPlayerInGame`, `+gentelem`, `+ForceReplayRecord`, `+showSounds`, `+showStreams`,
`+limitedUser`, plus `-track <name>`, `-series <name>`, `-startPos <n>`.

**The ArgManager splits on whitespace and does not honour quotes**, so a value containing a
space cannot be passed — `-series "2005 NEXTEL Cup"` is received as `'"2005'` and rejected
("not a valid series name"). `2005 NEXTEL Cup.gdb` carries `Series Default Filename = true`,
so omit `-series` and let it default.

Confirmed-playable launch (user-verified, full race at Daytona):

    NASCAR_GVR.exe -track Daytona -startPos 43

Tracks: `Bristol`, `Daytona`, `Indianapolis`, `Lowes`, `Phoenix`, `Talladega`.
Series `.gdb` files: `2004 CTS`, `2004 NEXTEL Cup`, `2004 NNS`, `2005 NEXTEL Cup`.
The OEM registry `cmd` is `-startPos 43 ` and the front end appends the track, so
`HKLM\SOFTWARE\GlobalVR\NASCAR` needs no `-track` of its own.

### 8.5 Asset gaps that are NOT installation faults

The game logs these as `SetError FATAL!` and races on regardless; none of them is in the
cabinet either, so they are OEM content gaps: `Daytona Load04.jpg` (only `Load01..03` ship),
`2005-DAYTONA500.clf` (Daytona and Talladega ship no `.clf`; Bristol, Indianapolis, Lowes and
Phoenix do), `POPUPQUIT0000.TGA`, `POPUPHEADER.TGA`, `VEHICLEDISPLAY.TGA`.

### 8.6 Notes on the tooling

* x32dbg's MCP server speaks JSON-RPC over `POST /mcp`; `GET /mcp` is an SSE stream and will
  hang a probe. `tools/list` returns 80 tools. `register_get` rejects the obvious parameter
  name — use `eval_expression` with `esp` instead.
* x32dbg pauses on TLS callbacks and DLL entry points by default, so any run loop must skip
  stops that are not its own breakpoints.
* Breaking on `ntdll!KiUserExceptionDispatcher` and reading `[esp]` as `EXCEPTION_RECORD*`
  yields code and address for every exception. `0x40010006` is only `OutputDebugString`.
* A first-chance stop whose EIP is a function prologue in a system DLL, with ECX equal to EIP,
  is a TLS-callback breakpoint, not a fault.
* `build.cmd` for the shim had lost the backslashes before `vswhere.exe` and `vcvars32.bat`:
  they had been written as literal 0x0B vertical tabs, which *display* as
  `Installerswhere.exe` and `Buildcvars32.bat`. Same class of trap as the `.def`
  `@`-truncation bug in section 5. Rewritten; no other file in the tree contains a stray 0x0B.

---

## 9. The shell, and the shell-side dongle gate (2026-10-03)

Established by decompiling the managed plug-ins and by an end-to-end run of the real cabinet
path, not by inference. This section replaces the earlier vague note that "the shell launches
the game using the registry".

### 9.1 `AMPlayer.exe` *is* GvrShell

It is not a stock Anark player. Its own build paths are `c:\dev\zeus\shell\gvrshell\...`, its
dialog titles say "GvrShell", and it announces itself to the GVR event log as
`HerculesInit( GvrShell )`. So the OEM `NASCAR_Shell.reg` and `GvrShell.reg` belong to **this**
binary — there is no separate, missing GVR shell executable. Everything the cabinet ran is
present.

Strings that locate the launch code (virtual addresses, see the base warning below):

| String | VA | Meaning |
|---|---|---|
| `In GvrGameLaunch` | `0x63DBCE40` | trace string inside the launch routine |
| `ERROR: %s trying to launch %s in %s` | `0x63DC0D5D` | its failure path: exe, folder, args |
| `Launch` | `0x63DC4F80` | the method name exposed to scene scripts |
| `.\PlugIns\%s.dll`, `GvrPlugInManager` | — | plug-in loading |
| `onGvrLoadComplete`, `onGvrControlUpdate`, `onGvrCustomBegin/End` | — | scene script event hooks |

**Its image base is `0x63B00000`, not `0x400000`.** This is the one thing that will silently
waste an afternoon: unlike `NASCAR_GVR.exe`, Ghidra and x32dbg addresses only agree if the base
matches, so confirm with `module_get_main` / `module_list` before trusting any address here.

### 9.2 The scenes are packed, which is why the registry names appear in no binary

Every `.am` file shares the magic `C3 0B 0A 00`. Searching the whole install for `launchName` or
`launchFolder` finds them **only** in the OEM reference `.reg` — because the literals live inside
the compressed scenes, which pass them to the plug-in at runtime.

Scenes present: `Shell.am` (648 KB), `NASCAR_Attract.am` (5.1 MB),
`NASCAR_Selection.am` (6.8 MB), `Launch_Game.am`, `Launch_Game_Error.am`, `DongleError.am`,
`WindowsRestart.am`. The existence of `Launch_Game.am` and `DongleError.am` as discrete scenes is
what makes the flow below legible.

### 9.3 The launch chain

```
Shell.am  ->  NASCAR_Attract.am  ->  NASCAR_Selection.am  ->  Launch_Game.am
   -> Operator_GetRegistry("launchName" / "launchFolder" / "cmd")   [NASCARPlugIn.dll, MANAGED]
   -> Launch(...)  ->  GvrGameLaunch  ->  CreateProcess             [AMPlayer.exe, native]
   -> NASCAR_GVR.exe -track <track> -startPos 43 ...
```

`Operator_GetRegistry` / `Operator_SetRegistry` read and write **arbitrary named values** under
`HKLM\SOFTWARE\GlobalVR\NASCAR`, with the value name supplied by the scene — hence the registry
plumbing is real even though no binary contains the names.

**Trap for the next person:** `NASCARPlugIn.dll` does contain a `CreateProcessA` call, but it is
inside `_runRegistrationProgram` and launches the **db_propagate / registration** tool, not the
game. Do not mistake it for the handoff.

### 9.4 Managed vs native — use the right decompiler

Ghidra produces convincing nonsense on the managed ones. Both managed plug-ins decompile cleanly
with the project's ILSpy setup (`Tools\Decompile-Managed.ps1`, or `ilspycmd` from
`re-agent\Tools\ilspy`).

| Binary | Kind | Notes |
|---|---|---|
| `AMPlayer.exe` | native x86 | GvrShell; the launch code |
| `NASCARPlugIn.dll` | **MANAGED** | 680 KB; the **operator menu** |
| `GvrPlusDEPlugin.dll` | **MANAGED** | Plus accounting |
| `GvrScriptedInputPlugin.dll` | **MANAGED** | |
| `GvrDiagnosticsPlugIn.dll` | **MANAGED** | |
| `PLUSDE.dll` | **MANAGED** | mixed-mode; see section 5c |
| `DongleStorageDevice.dll` | **MANAGED** | mixed-mode, HASP statically linked |
| `ZeusIOPlugIn.dll`, `HerculesPlugIn.dll`, `GvrMusicPlugIn.dll`, `GvrDirtyWordPlugIn.dll`, `GvrLinkingPlugIn.dll`, `GvrMotionPlugIn.dll`, `GvrIO.dll`, `Dongle.dll` | native | |

Decompiled here: `NASCAR\Decompiled\NASCARPlugIn\` (40,507 lines) and
`NASCAR\Decompiled\DongleStorageDevice\` (636 lines).

### 9.5 `NASCARPlugIn.dll` is the operator menu

Its script-callable surface is the service/operator API, not gameplay:
`Operator_Init`, `Operator_Shutdown`, `Operator_Get/SetRegistry`,
`Operator_Get/SetCabinetConfig_Int/Float`, `Operator_Get/SetGlobal_Int`,
`Operator_CoinService_CoinEnable`, `Operator_GetCreditsOnly`, `Operator_ResetCredits`,
`Operator_Reset/GetServiceCredits*`, `Operator_Set*PriceModel`, `Operator_Get/SetTimeZone`,
`Operator_Set/Reset/IsWindowsRestartTime*`, `Operator_Reg_*` (propagate / modem / status),
`Operator_GetDongleCabinetId`, `Operator_GetDongleVersion`, `Operator_DongleCountryMatches`,
`Operator_DeleteAllTrackRecords`, `Operator_ResetFactoryDefaults`, `Operator_FirstTime_Init`,
`Operator_DebugMessage[String]`.

Note it is riddled with `GvrPlusConstant.*_PGA4` types (`CourseStats_PGA4`, `HoleResult_PGA4`,
`GamePurchaseStats_PGA`), confirming this shell derives from the **PGA golf** codebase — which
also explains the dead `C:\NASCAR_TOUR_GOLF_2006\...` paths in the operator-menu XML.

### 9.6 The shell-side dongle gate — a DIFFERENT check from the game's

This is the current blocker, and it is **not** the gate solved in section 8.3. That one is inside
`NASCAR_GVR.exe` and is satisfied by the 39-byte stub in `GvrIOShim`, which verifies the host
module's bytes and therefore deliberately will not touch `AMPlayer.exe`.

The shell's route:

```
Operator_GetDongleCabinetId                              [NASCARPlugIn.dll]
  -> new GvrSmartDevice(engine, gameName, GVRSDType = 2)  [2 = DONGLE]
  -> GvrSmartDevice.ReadHeader
  -> GVRStorageDevice front end
  -> DongleStorageDevice.dll                              [MANAGED, HASP statically linked]
  -> hasp(service, seed, lptnum, pass1, pass2, &p1,&p2,&p3,&p4)
```

HASP services used: **6** (presence / id), **50** (0x32, memory read), **51** (0x33, memory
write). The decisive logic is tiny:

```c
IsPresent() : for each of two password pairs: hasp(6,300,0,pass1,pass2,...)
              return true when (p3 == 0 && p1 != 0)
GetType()   : return state[+16] ? 2 : 0      // 2 = DONGLE, only once HASP was found
GetSize()   : 112                            // the HASP memory image, in bytes
Read()      : returns 5000 unless state[+16]; else hasp(50,...) in 16-bit words
```

Everything cascades from that one presence call. With no hardware: `IsPresent` is false,
`GetType` returns 0, the device is not a dongle, and the shell shows `DongleError.am` and exits.

`GVRSDType` constants (from section D / the ABI report): **0 = PLAYER, 1 = OPERATOR,
2 = DONGLE, 3 = HOURLY**.

### 9.7 Why the obvious file swap does not work

`DongleStorageDevice.dll`, `GVRSDEmulator.dll` and `GVRSCR28.dll` all export **exactly the same
two functions** — `CreateGVRStorageDeviceImp` and `ReleaseGVRStorageDeviceImp` — behind the same
`GVRStorageDevice` front end, so they are interchangeable back ends. That makes a drop-in
replacement the natural fix.

But copying `GVRSDEmulator.dll` over `DongleStorageDevice.dll` will **not** work:
`GVRSDEmulator`'s `GetType()` returns **0 (PLAYER)**, so the shell would see a blank player card
rather than a dongle. Its other documented defects are that `GetId()` never writes its out-param
and its RAM image is zeroed every run, so nothing persists. See `GVRSCR28_ABI_REPORT.md` §D for
its full per-slot behaviour — it remains the best *reference implementation* of the vtable.

A correct replacement therefore needs: `GetType()` → 2, `IsPresent()` → true, `GetSize()` → 112,
a stable `GetId()`, and file-backed `Read`/`Write` over a 112-byte image whose contents satisfy
`GvrSmartDevice.ReadHeader` (schema-driven — see the ABI report §B.3). Deploy as
`DongleStorageDevice.dll` with the OEM kept as `DongleStorageDevice_oem.dll`, per the usual
drop-in convention.

### 9.8 End-to-end test result (2026-10-03)

Launched the real path, `NascarLaunch.exe` → `AMPlayer.exe "Shell.am" -v FULL`, and monitored
every child process for 240 s:

* the shell boots and is healthy — 118 MB, 29 threads, responding, which matches the known-good
  attract state
* GVR event log records `HerculesInit( GvrShell )` then `HerculesInit( AMPlayer )`
* the registry carries `launchFolder`, `launchName` and
  `cmd = -startPos 43 -windowed -width 1280 -height 720 -noabort -notimeout`
* **`NASCAR_GVR.exe` never spawned, and no new `trace00N.txt` was written** — confirmed twice,
  by process monitoring and by the filesystem, since the game writes a trace on every launch
* the shell reported the dongle missing/invalid and exited

So the whole chain works except the shell-side dongle back end. Nothing else is missing.

---

## 10. The shell's dongle self-test — fully traced (2026-10-03)

The game (`NASCAR_GVR.exe`) and the shell (`AMPlayer.exe`) check **two different dongles**. The
game's is an Aladdin **HASP**, statically linked, already satisfied by the 39-byte stub in
GvrIOShim (section 8.3). The shell's is a GVR **smartcard** read through `GVRSCR28.dll`, and it
is the current blocker. They are unrelated; a fix for one does nothing for the other.

### 10.1 Where the on-screen message comes from

`Shell.am` carries, in UTF-16 at offset 0x19CC, a startup **self-test** screen with the fields
`DongleGame`, `DongleVersion`, `DongleRegion` and `ErrorMessage = "DONGLE MISSING OR INVALID"`.
A sibling condition in the same window is `"There is a CD in the drive."`, confirming this is a
general power-on self-test, not a one-off. The DLL hits for "missing or invalid" are unrelated
PC/SC error-table boilerplate inside GVRSCR28/PCSCSCR2 — the scene is the real source.

When the user sees the error, the three fields read `Game: n/a  Version: n/a  Region: n/a` —
i.e. all three dongle accessors returned empty.

### 10.2 The call chain, and the single upstream gate

The scene calls three methods exposed by `GvrPlusDEPlugin.dll` (managed):

```
IsValidDongle()      -> GvrSmartDevice(engine,"NASCAR",GVRSDType=2); DongleInserted(0);
                        ReadHeader(); GetMemObject("Header.GameName");
                        strstr(first 8 bytes,"NASCAR") ? true : false
GetDongleID(out)     -> ...; DongleInserted(0); GetId(); format as string
GetDongleData(k,out) -> ...; DongleInserted(0); read field k
```

All three gate on **`GvrSmartDevice.DongleInserted()`** (in `PLUSDE.dll`) FIRST. Measured: with
a software `GVRSCR28.dll` device deployed and logging every call, after `DongleInserted` the
device was interrogated **zero** times — no IsPresent, GetType, GetId, Connect, GetStatus or
Read. So `DongleInserted` returns false before the storage device is ever touched, which is why
every device-level fix was downstream of the real decision.

### 10.3 Everything that was eliminated, and why

Each ruled out one hypothesis; the method matters for whoever picks this up:

| Attempt | Result | What it proved |
|---|---|---|
| swap stock `GVRSDEmulator` in as `GVRSCR28.dll` | still rejected | `IsPresent=true` alone insufficient |
| patch emulator `GetType` -> 2 (DONGLE), live | still rejected | presence+type insufficient |
| provision `GvrDongle`/`DongleInfo`/`GvrCabinet`/`CabinetBilling` rows | still rejected | not a DB-row lookup |
| set GlobalVariable `CabinetId=1` | still rejected | not that variable |
| SQL trace (GVRSQLITE_LOG) | 32 GlobalVariable reads, **no dongle query at all** | the check never hits SQL |
| own software device (good GetId, GetType=2, file-backed) | **0 interrogation calls** | gate is upstream of the device |
| patch `GvrPlusDEPlugin.IsValidDongle` -> return true (dnlib NativeWrite) | plug-in loads, still rejected | self-test uses the field accessors, not just IsValidDongle |

### 10.4 Why the data is genuinely absent

`GetMemObject("Header.GameName")` needs the field `Header.GameName`, which is defined **nowhere**
on a dongle-less cabinet: the only file in the whole install containing that string is
`GvrPlusDEPlugin.dll` itself (the literal it passes). Its definition lives on the real dongle's
own header. NFSU's `PlusData.gvr` defines `Header.PlayerInfo.*` but not `Header.GameName` either.
So the fields the self-test prints (`GameName`, `Version`, `Region`) come off the dongle, and
without one they are simply not present.

### 10.5 Corroboration from an external source

A collector's HASP dump list gives NASCAR's dongle contents as `NASCAR1.5CTRYXX_CAB0` (also
`1.0`/`2.0` variants). This matches exactly:

* `IsValidDongle` does `strstr(Header.GameName[0..7], "NASCAR")` — first 8 bytes `NASCAR1.`
  contain `NASCAR`. PASS.
* `1.5` matches this cabinet's `RUNTIMEOEMVERSION = 1.5.0.04`.
* `CTRYXX` is the region field (`XX` = region-free), `CAB0` the cabinet type.

So a genuine dongle passes because `Header.GameName`/`Region`/`Version` decode to real values.
That is the data a content-reconstruction fix must reproduce.

### 10.6 Artifacts produced

* `NASCAR\src\GvrDongleEmu\` — software `GVRSCR28.dll` back end (type 2, working GetId,
  file-backed image, logs every call via GVRDONGLE_LOG). Correct, but the gate never reaches it.
* `NASCAR\Tools\Patch-GvrDongleCheck.ps1` — dnlib/NativeWrite patcher for managed boolean gates
  (idempotent, keeps `*_oem.dll`). Patched `IsValidDongle` cleanly but that alone is insufficient.
* `Tools\Provision-GvrDongle.py` — schema-driven dongle-row provisioner (reusable; not the fix here).
* `GvrSqlite.cs` log path fixed: honours `GVRSQLITE_LOG` as a path, defaults to `%TEMP%`
  (was a hardcoded `C:\gvrsqlite.log` that failed silently without admin — a real bug).

### 10.7 Current state / next

Two routes remain (see NASCAR_PLAN.md dated entry):
1. continue patching the `PLUSDE` chain from `DongleInserted` upward (risk: PLUSDE is the shared
   DB core; unknown how many layers);
2. **content reconstruction** — build a `Header.*` image the UNPATCHED `DongleInserted`/`ReadHeader`
   accept, served by our `GVRSCR28.dll`, so the self-test passes on data rather than on removed
   checks. Preferred: real dongle contents satisfy every check at once. Needs the header
   layout/schema (the ABI report flags this as not yet recovered).

Install currently has: patched `GvrPlusDEPlugin.dll` (OEM at `_oem`), software `GVRSCR28.dll` +
`PCSCSCR2.dll` (OEMs at `_oem`). None of these is load-bearing yet; all are reversible.

> **Superseded by section 11.** Section 10's conclusion that the gate sits "upstream of the
> device" was wrong: the shell's self-test never uses the PLUSDE path at all.

---

## 11. The shell runs end to end (2026-10-03, night)

Starting from "DONGLE MISSING OR INVALID", the shell now passes every power-on check, loads
`NASCAR_Attract.am` ("NASCAR Start Your Engines"), `NASCAR_Selection.am` and the operator menu
(user-confirmed). Each blocker in order, with the fix that is now on disk in `D:\Games\NASCAR`.

### 11.1 How to see what the shell is doing (read this first next time)

* **The scene scripts are readable.** Every `.am` embeds its JavaScript behaviours as **zlib
  streams** (`78 9C` / `78 DA`). `Tools\Extract-AmScripts.py <file.am>` inflates them all; then
  grep. In `Shell.am` the startup self-test is `Behavior95` (stream at `0x28873`), the dongle
  slide logic `Behavior92` (`0x4dad3`), cabinet linking `CabinetComm` (`0x8a130`). They name every
  slide, every check and every `Gvr.Call` — far faster than binary archaeology.
* **Slides of `Shell.am`:** `Checks` ("Please Wait..."), `Steering`, `PedalCalibrate`,
  `DongleError`, `CDError`, `NotSynced`, `CardReaderError`, `CardDispenserError`, `IOBoardError`,
  `MotionError`, `IOBoardVersionError`, `CabinetNumberSelect`.
* **The startup state machine** (`startUpChecks` in `Behavior95.onUpdate`):
  1–3 dongle game/version/region → **4** shell init (`NASCAR_INITIALIZE_SHELL`, uptime stats,
  service credits, restart bookkeeping, `GammaSet.exe`, `CabinetComm.startComm()`,
  `NASCAR_CD_IN_DRIVE`) → **7–9** IO board (`CoinError != 3`, `IOBoardMajorVersion >= 3`,
  `IOBoardMinorVersion >= 3`, after a deliberate 15 s `ioBoardDelay`) → **11** resolution
  (`rundll32 nvcpl.dll,dtcfg setmode`) + pedal-range check → 12 steering (only after an
  unscheduled Windows restart) → 13 bad buttons + cabinet-number collision → 14 motion → **15**
  `Gvr.LoadPresentation(path + "//NASCAR_Attract.am")`.
* **A script exception is silent.** If any `Gvr.Call` throws, the rest of that frame's
  `onUpdate` is skipped and the *same state re-runs next frame, forever* — the screen sits on
  whatever slide was showing (or black). Nothing is logged by default.
* **Anark's logger is a developer-only COM server** that never shipped: `AMPlayer.exe` calls
  `CoCreateInstance({DE411D6F-2BDA-444C-AC87-9171BAFD9E99}, IID {18725949-89C4-460C-A092-75520BFFB542})`
  and prints `FAILED to create AKLog2 interface.` **`Tools\AKLogShim\`** is a stand-in, registered
  per user (no admin), that forwards the player log to `OutputDebugString` **and arms a vectored
  first-chance exception logger** (module+offset, C++ type name, short stack). Slots AMPlayer
  uses: `+0x1C (a,b)`, `+0x24 ()`, `+0x28 (record*)` — the record is 0x42C bytes, UTF-16 message at
  `+0x8`, source file at `+0x21C`. It refuses `AKPlu*.dll` callers (they may use slots whose
  signatures are unknown). It is what found the per-frame exception in 11.4. Register:
  `SysWOW64\reg.exe add "HKCU\Software\Classes\CLSID\{DE411D6F-2BDA-444C-AC87-9171BAFD9E99}\InprocServer32" /ve /d <path>\AKLogShim.dll`
  plus `/v ThreadingModel /d Both`. Delete that key to remove it.
* **`Tools\dbwin.py <out> <seconds>`** captures `OutputDebugString` from every process;
  **`Tools\stacksample.py <pid> [n] [tid]`** samples a WOW64 thread's EIP and stack return
  addresses. The scene runs on the **CController** thread (its id is logged as
  `Running Thread CController 0x...`), not the window's main thread — sample that one.
* `DebugMode` (`HKCU\Software\Anark\Client\3.0\Preferences`, DWORD) exists but shows nothing on its
  own, and the right-click popup menu has no console.
* **Unelevated, `AMPlayer.exe` is registry-virtualised**: its HKLM writes land in
  `HKCU\Software\Classes\VirtualStore\MACHINE\SOFTWARE\WOW6432Node\GlobalVR\NASCAR`, and its reads
  prefer that copy. Look there for the script's `RegWrite` side effects (`Result_Status`,
  `UpTimeStart`, `MaxLinkId`, `FirstTimeStartup`, `FullScreenWidth/Height`).

### 11.2 The dongle: the reader is `Dongle.dll`, not PLUSDE

`Behavior95` calls `Gvr.Call("CheckDongle")` → `ZeusIOPlugIn` → GvrIO → **`Shell\bin\Dongle.dll`**
(a GvrIO plug-in, PDB `c:\Dev\Zeus\Shell\GvrIO\Release\Dongle.pdb`) and tests
`Gvr.DongleGame == "NASCAR"`, **`Gvr.DongleVersion == "1.0"`** (not 1.5), and
`Gvr.DongleRegion` ∈ {`US`, `XX`}. The PLUSDE accessors (`GetDongleData` / `GetDongleID`) appear
only in `OPERATOR_MENUS\XML\*`. PLUSDE picks the storage back end by type — `GVRSCR28.dll` for
types 0/1/3, **`DongleStorageDevice.dll` for type 2** — which, not an upstream gate, is why the
`GVRSCR28` software device saw zero calls in section 10.

`Dongle.dll` embeds the **same HASP record library as `NASCAR_GVR.exe`** (section 8.3):
`0x10004660(idx)` zeroes a 27-byte record at `0x10006068 + idx*0x1B` and calls the filler
`0x10004410`, which reads the 112-byte key memory `M` (HASP services 1, 5, then 0x32 for 0x38
words) and slices it: `rec[1..6]=M[0..5]` game, `rec[8..A]=M[6..8]` version,
`rec[C..D]=M[13..14]` region, `rec[10..13]=M[9..12]`, `rec[15..17]=M[16..18]`,
`rec[19]=M[15]`. Region codes the game accepts: `US CA UK EU AU IE IT NZ ZA XX`. Plug-in message
IDs: `0x2`/`0x23` presence (fires callback `0x24`), `0x25` present flag, `0x26` game, `0x27`
version, `0x28` region, `0x29` cab, `0x4E` set index, `0x12345678` re-read. sMessage: `+4` id,
`+8`/`+0xC` args, `+0x14` handled.

**Fix:** `Tools\Patch-DongleDll.py <Shell\bin>` replaces only the filler with a 28-byte
position-independent stub that copies a record built from the image **`NASCAR1.0CTRYXX_CAB0`**
(relocations inside the stub neutralised; OEM kept as `Dongle_oem.dll`; `--restore` undoes it).
With `1.5` (the collector's dump) the shell says "Invalid Dongle: Incorrect Game Version".

### 11.3 "Please Wait..." forever — `NASCARPlugIn.dll` never loaded

`NASCARPlugIn.dll` imports **`MSVCP71D.dll`** (the debug C++ runtime, which needs
`MSVCR71D.dll`). Both ship in the disc's `WINDOWS\system32` file group but were never staged, so
the plug-in failed to load silently (no `Loaded .\PlugIns\NASCARPlugIn.dll` line), every
`NASCAR_*` call failed, and case 4 never advanced. **Fix:** stage both app-local in `Shell\bin`.

### 11.4 Black screen — `DateTime.Parse("")` every frame

Case 4 reads `UpTimeStart` / `UpTimeEnd` and calls `NASCAR_STATS_UPDATE_UP_TIME`, whose
`Stats_Update_UpTime` does `DateTime.Parse(startTimeStr)`. On a fresh install both are empty →
`FormatException` every frame, and the writes that would fix it come *after* the call. A real
cabinet seeds them through the `FirstTimeStartup = 1` first-boot branch. **Fix:** seed both
(`yyyy-MM-dd HH:mm:ss`) in `HKLM\SOFTWARE\(WOW6432Node\)GlobalVR\NASCAR` — or, for an unelevated
test, in the VirtualStore copy. After one good pass the shell maintains them itself.

### 11.5 Host-hostile behaviour inside `NASCARPlugIn.dll` (the game-only rule)

* **It changes the PC's time zone.** Its settings init applies the cabinet zone
  (`"Turkey Standard Time"`, UTC+3) with `SetTimeZoneInformation` — it did so on the dev machine
  (and on 2026-07-20). **Fix:** rename the import to `GetTimeZoneInformation` (file offset
  `0x8D442`, `S`→`G`; same length, compatible signature; the assembly is not strong-named).
* **`NASCAR_CD_IN_DRIVE` = `CreateFileA("D:\")`** (native, `0x1000CF50`) — on the cabinet `D:` was
  the CD drive. Any PC with a `D:` drive gets "There is a CD in the drive". **Fix:** string
  `"D:\"` → `"?:\"` (file offset `0x38380`, single reference).
* It imports `ChangeDisplaySettingsExA`, and case 11 runs
  `rundll32 nvcpl.dll,dtcfg setmode 1 <w> <h> 32 60` (frame 1 also runs `setmode 1 800 600`).
  Modern NVIDIA drivers reject `dtcfg` (result 8, desktop unchanged on an RTX 3060 laptop); old
  ones would obey. Case 11 also writes `FullScreenWidth/Height` (1360×768 here).
* Case 4 launches **`Shell\GammaSet.exe -gammasetting 3 -digitalvibrance 15`**; a 32-bit
  `NvCpl.dll` existed on the dev host, so it can really act. **Fix:** rename to
  `GammaSet.exe.arcade-disabled`, as NFSU's `Disable-GammaSet` does.

Both one-byte patches are applied to `Shell\bin\plugins\NASCARPlugIn.dll`; the OEM is kept as
`NASCARPlugIn_oem.dll`.

### 11.6 IO board and pedal calibration — a `GvrIOMessageSend` overlay

The shell's `GvrIO.dll` is byte-identical to the game's OEM copy, so `src\GvrIOShim` applies
unchanged (its dongle patch verifies the host's bytes and leaves `AMPlayer` alone). It now also
overrides `GvrIOMessageSend`; every other export still forwards to `GvrIO_oem.dll`:

| Message (ZeusIOPlugIn getter) | No hardware returns | Shim answers |
|---|---|---|
| `0x2F` IO board major (`0x100023A0`) | 0 | 3 |
| `0x30` IO board minor (`0x100023E0`) | 0 | 3 → firmware 3.03 |
| `0x39` axis min; `+8` = axis (0 X steering, 1 Y gas, 2 Z brake) | 255 | 0 |
| `0x3A` axis max | 1 | 255 |

The script needs major ≥ 3, minor ≥ 3, and `AxisMax − AxisMin ≥ 40` for gas and brake, or it
shows `IOBoardError` / `IOBoardVersionError` / `PedalCalibrate`. Answers are substituted **only
when the OEM's is missing or below the threshold** (axis: OEM max < 40), so a real board or wheel
always wins. `GVRIOSHIM_NO_IOBOARD=1` disables the overlay. Note `ZeusIOPlugIn`'s property table
is `{getter, setter, name, type, 7}` but reads off by one entry if taken naively — disassemble the
getters. `CoinError` belongs to `HerculesPlugIn` and was not 3. The rebuilt shim is deployed to
`Shell\bin` only; the game's `Game\GvrIO.dll` is the previous build.

### 11.7 ~4 fps in the shell — `HerculesPlugIn` waiting on a coin monitor that is not there

Measured, not guessed: the shell used ~19 % of one core while showing ~4 fps on either GPU, and
stack samples put half of the main thread's time in `WaitForSingleObject` inside
`HerculesPlugIn.dll` (helper `0x100048E0`, reached from `0x10006665` and from
`0x1000219B` = `connection->Receive(1000)`, `push 0x3E8`). Hercules talks to `GVRCoinMonitor.exe`
over a shared-memory `MemoryConnection` (`%s_map` / `%s_event` / `%s_mutex`), and every poll
waited out its timeout because the monitor is not running ("ERROR: GvrCoinMonitor is NOT
Running."). The plug-in is a **debug build** (`0xCC` stack fill, `_RTC_CheckEsp`).

**Not the fix:** running `Hercules\GVRCoinMonitor.exe` from the disc — it is a cabinet daemon
that writes `C:\Hercules\CoinCount.bin` and can inject keystrokes (`keybd_event`).
**Fix:** the single wait helper `0x100048E0` takes its timeout from `[ebp+0xC]`; patch
`8B 45 0C` (`mov eax,[ebp+0xC]`) → `33 C0 90` (`xor eax,eax; nop`) at file offset `0x4904`, so
every Hercules wait is a zero-timeout poll. All ten callers are the connection's mutex/event
waits; an uncontended mutex is still acquired instantly. OEM kept as `HerculesPlugIn_oem.dll`.
CPU went 19 % → 41 % of a core (rendering instead of waiting).

Also measured on this hybrid laptop: `AMPlayer` renders with OpenGL and defaulted to the Intel
iGPU (`ig9icd32.dll`). `HKCU\Software\Microsoft\DirectX\UserGpuPreferences`
`"<path>\AMPlayer.exe" = "GpuPreference=2;"` (and the same for `NASCAR_GVR.exe`) moves it to the
NVIDIA GPU (`nvoglv32.dll`). That alone did **not** fix the frame rate — Hercules did.

**The same wait, a second copy, in the operator menu.** After the Hercules patch some operator
menus were still slow. `NASCARPlugIn.dll` statically contains its own copy of the Hercules
client: `MemoryConnection::Receive(timeout)` at `0x1002D0A0` does
`WaitForSingleObject(receiveEvent, timeout)` and the operator code calls it with 1000 ms
(`push 0x3E8` at `0x1002C1C4`) whenever a menu asks the coin monitor for meters/coins.
**Fix:** at file offset `0x2D0AC`, `8B 4C 24 08` (`mov ecx,[esp+8]`) → `33 C9 90 90`
(`xor ecx,ecx`), so the receive is an instant "no message". Only `Receive` is touched (not the
mutex waits). This is the third byte patch in `NASCARPlugIn.dll`.

### 11.8 Still open

* ~~`NASCAR_Selection.am` loads `c:\NASCAR\Shell\...` by absolute cabinet path~~ — fixed, section 12.
* Starting a race from the shell works (user-confirmed: a full race at 23:38–23:43 ended in
  `~ fini ~` and returned to the shell); it was simply reached via the car-select timeout.
* None of 11.2–11.6 is in `Install-NASCAR-GVR-Portable.ps1` yet.

---

## 12. Operator menus and car select (2026-10-04)

User-confirmed end state: operator Info loads ("Region: United States", IO Board 3.03), and car
select shows the drivers. Three separate defects stacked on the car-select screen; each was
invisible until the one before it was fixed.

### 12.1 The trap behind most of it: a missing `Gvr` method or property aborts silently

When a scene script (or an operator-menu XML `<LOGIC>`) uses a `Gvr` method or property that no
loaded plug-in provides, Anark **silently abandons the whole call chain** — no exception, no
"Script Error" record, nothing in any log. The screen just stops where it is. Two plug-ins are
deliberately not loaded on a desktop install (`GvrLinkingPlugIn`: cabinet-to-cabinet linking,
it hid the window at 100 % CPU; `GvrMotionPlugIn`), so everything that reaches for them is a trap.

* `Tools\Find-AmMissingCalls.py <Shell>` lists every `Gvr.Call("<method>")` in every `.am`
  that no loaded plug-in (or `AMPlayer.exe` itself) provides. Remaining after the fixes below,
  all on screens a desktop install does not use and none patched: `PGA_*` (golf leftovers on the
  Propagate / Modem Test screens), `ACCOUNTING_IS_PLUSPOINTS`, `NASCAR_GET_PHANTOM_CREDITS`,
  `NASCAR_TOURNAMENT_GET_LOCAL` (smart-card purchase screens), `MotionMotor` (Motion Test).
* For the operator XML: scan `<GETPROPERTY>` / `<CALL METHOD=>` names the same way.

### 12.2 Operator → Machine Settings → Info stuck on "Loading. Please Wait..."

The dialog is raised when an item is activated and cleared only by "table loaded....". The Info
table's MAC-address cell is `<GETPROPERTY>MacAddress</GETPROPERTY>` — a `GvrLinkingPlugIn`
property. **Fix:** that cell → `<STATIC_TEXT><VALUE>N/A</VALUE></STATIC_TEXT>` in
`OPERATOR_MENUS\XML\MachineInfoCol.xml`; likewise the four
`<CALL METHOD="NASCAR_GET_CONTACT_HEADER">` cells (provided by nobody) in `SysStatusCol.xml`
→ static blank. OEM kept as `*.xml.oem`. Not yet fixed, untested:
`MachineSettingsCol.xml` uses `<GETPROPERTY>MotionForce</GETPROPERTY>` (`GvrMotionPlugIn`) as a
dropdown value. Operator-menu keys (from the screen legend): View `V` = up, Music `M` = down,
Start `S` = select, Look Back `L` = back.

### 12.3 Car select ("Start Your Engines", then a 24 s timeout auto-picks a car)

`NASCAR_Selection.am` slides: `SelectDriver` → `WipeEnd` → `SelectTransmission` → `SelectTrack`
→ `Lobby` → `LoadGame`. "Start Your Engines" with an empty white bar **is** `SelectDriver`: the
bar is where the driver-name image slides in, and a 3D car slides in over the background. The
`Timeout` behaviour picks a car after 24 s, which is why the user only ever chose a transmission.
`DriverSelect_Behavior` steers with `Gvr.AxisX` (wheel) and selects with the gas pedal
(`Gvr.AxisY`) or Start.

1. **Absolute cabinet paths.** 24 car-skin / driver-name literals
   `"c:\\NASCAR\\Shell\\Textures\\..."`, plus `Gvr.MusicDir = "c:\\NASCAR\\Shell\\Audio"` and two
   slide backgrounds. Fixed with `Tools\Patch-AmPaths.py --map "c:/NASCAR/Shell/Textures/=Textures/"
   --map "c:/NASCAR/Shell/Audio=../Audio"`. Anark resolves `remoteSource` relative paths against the
   **.am's folder** (`Shell\`); the native music plug-in resolves `MusicDir` against the **working
   directory** (`Shell\bin`). An absolute install path did not fit (see 12.5). Menu music came
   back with this fix.
2. **The cabinet was "international".** `NASCAR_IS_INTERNATIONAL` (native `0x10009780`) is
   `strcmp(countryCode, "XX") == 0`. `countryCode` defaults to `"US"` and is overwritten only when
   `Operator_Init` reads a PLUSDE dongle header successfully. The leftover software
   `DongleStorageDevice.dll` (GvrDongleEmu, section 10) answered with a blank image, the region
   lookup fell through to `"XX"`, the operator Info page said "Non-US", and `onInitialize`'s
   international branch (`Driver_Group.detach(KaseyKahne / JeffGordon / JimmieJohnson /
   KyleBusch)`) raised script errors every frame. **Fix:** restore the OEM `DongleStorageDevice.dll`
   (the software one is parked as `DongleStorageDevice.dll.gvrdongleemu`): with no HASP key its
   header read fails, the block is skipped, the country stays `"US"` — all 12 drivers.
3. **The real blocker: `CabinetComm.sendPacket()` → `Gvr.Call("SendPacket")`.**
   `resetDrivers()` → `updateDriver()` → `scene.CabinetComm.sendPacket()` ends in a
   `GvrLinkingPlugIn`-only call, which aborted the chain every frame (12.1): `firstFrame` never
   cleared, `frameCounter` never reached 5, so `onPlaySelectAnim` (which slides the car and name
   in) never ran — while `resetDrivers` re-assigned the car skin 60 times a second.
   **Fix:** `Tools\Patch-AmCalls.py --method SendPacket <Shell>` replaces that statement with `;`
   in all **17** scenes that carry `CabinetComm` (Shell, Attract, Selection and 14 operator
   screens; OEMs kept as `*.am.oem`). Skin loads on entering car select dropped from thousands to 7.
4. **Steering on a PC.** `Gvr.AxisX/Y/Z` = GvrIO message **5** on the analog device (`+0` = 1,
   `+8` = axis). The shell's `GvrIO.dll` shim now answers it when no analog device exists —
   decided from the **gas** axis max (< 40), because the OEM's uncalibrated steering max is ≥ 40
   and its garbage value would otherwise pass through — with centred values, and while a window
   of the process has focus `←`/`D` = −60, `→`/`G` = +60 for steering. A real wheel wins.

How it was found: inflate the scripts (`Extract-AmScripts.py`), then `Tools\Edit-AmScript.py`
to insert checkpoint `output()` lines into `DriverSelect_Behavior` (recompressed at exact size,
`--strip-comments` frees room). The checkpoints printed "before updateDriver" every frame and
"after updateDriver" never. AKLogShim's script-error decoding (the record carries a va_list at
`+0x210`: behaviour name + line) located the international-branch errors; script line *N* is the
*N*-th line after the line holding `<![CDATA[`. Ruled out: the renderer (Anark's right-click
Rendering Engine → DirectX looked identical; the `DisableGL` preference does not switch it).

### 12.4 Reading the car-select screen

`NASCAR_Selection.am` behaviours: `DriverSelect_Behavior` (stream `0x23dd0d`), `Selection_Behavior`
(per-driver selector timeline, fires `onPlaySelectAnim` after `bigHeadPause` = 12 frames at the
centre key), `TrackSelect_Behavior`, `TransmissionSelect_Behavior`, `Timeout`, `Header_Behavior`
(slide exits + `Stats_*Time` registry writes), `CabinetComm` (linking), `Globals`
(`DoMotionResize` shrinks layers on motion cabinets only). Track list on the disc: Daytona,
Talladega, Lowes, Indianapolis, Phoenix, Bristol (the selector table names ~30 tracks; only six
are enabled).

### 12.5 The `.am` container, for anyone editing scenes

Tag-length-value records. Script records: tag `0x1770`, LE32 length = **uncompressed** size + 22,
then 4 zero bytes, `ANRK`, 4 zero bytes, LE32 uncompressed size, then a zlib stream. There is
**no compressed-size field**, while enclosing records store **physical** lengths (the root's
length = file size − its header). So a rewritten record must keep exactly its original
compressed byte count; `Patch-AmPaths.py`'s `deflate_exact` does that (exact-size full
compression, else a normal head + a raw stored tail sized to fit, else a sync-flush split), and
only the record's two uncompressed-size fields change. A replacement *longer* than the original
usually cannot fit — prefer shorter text (relative paths, `;` instead of a call) or strip
comments. In replacement strings use a function, not a string: `re.sub` would treat the doubled
JS backslashes as escapes (that bug produced `..Textures`).

### 12.6 Still open

* ~~Track select is skipped; every race is Daytona~~ — **RESOLVED 2026-10-04 (user-confirmed:
  tracks selectable, a race logged on a second track in `TrackStats_NAS1`).**
  `TrackSelect_Behavior` (frame 5) calls `trackSelectedByOther()` — auto-pick, no screen — when
  `tracksEnabledCount <= 1`. `Globals` builds that count from
  `Gvr.Call("NASCAR_DISABLE_TRACKS_GET_VALUE", i)`, i = 0..5 (Daytona, Talladega, Lowe's,
  Indianapolis, Phoenix, Bristol). That method (native `0x1000D590`) returns
  `(cabConfigInt(0x1E) & bitTable[i]) > 0` — **despite the name, a set bit = track AVAILABLE**.
  Config int 30 **is** `CabinetConfiguration_NAS1.TracksDisabled`, which the OEM row ships as 0
  (= no tracks). Bit tables: country ≠ "XX" → `0x10096848` = {1, 2, 4, 0x10, 0x20, 0xFFFFFFFF};
  "XX" → `0x10096830` = {1, 2, 4, 8, 0x10, 0x20}; **63 (0x3F) enables all six**. The user set it
  through **Operator → Game Settings → Disable Tracks**; `Build-NascarSqliteDb.py` now provisions
  63. (A first attempt that set the column directly was wrongly recorded as "no effect": that
  test run went into the operator menu and never reached track select, and the value was then
  reverted.) With 63 the Disable Tracks screen builds normally — six names, six `GET_VALUE` = 1.
* The Disable Tracks screen "froze" once while the value was 0 (menu beeps, screen stopped);
  not reproduced since and the user has used it successfully. Its table
  (`TracksDisabledCol.xml`) uses only `NASCARPlugIn` methods.
* **"Game Crashed" after a race = `Result_Status` 0.** `Launch_Game.am` maps 0 → "Game Crashed",
  1 → finished, 2 → timed out; the game writes 0 for any unfinished race. `trace014.txt`: race
  reached `INGAME` at 7.15 s, then `#### ESCAPEREQUESTED ####` and a clean `~ fini ~` — an abort,
  not a crash. Source of the escape unconfirmed (user Esc/Q?). The shell's own command line:
  `-track DAYTONA -car 20 -mode SINGLE -racelength 0 -nofe -series 2005NEXTEL -handling 0`, to which
  the registry `cmd` is appended.
* Keyboard steering in car select is wired (axis 0 is now queried and answered) and the user
  scrolled drivers/transmissions with it (`onSelectionChanged` events in the capture).
* `MotionForce` dropdown (12.2) and the unused-screen calls listed in 12.1.
* Everything in sections 11–12 still has to go into `Install-NASCAR-GVR-Portable.ps1`.

---

## 13. Display: resolution limit, borderless, always-on-top, taskbar (2026-10-04, evening)

User-confirmed: the race is sharp at 1920×1080, borderless, and alt-tab / taskbar behave.

### 13.1 The engine drops every display mode larger than 1600

The race's video manager constructor (`0x507C46`) sets the accepted mode range — min 640×480,
**max dimension `[mgr+0x1F64]` = 1600** (`0x507C5A: C7 82 64 1F 00 00 40 06 00 00`), plus an
unused-here `+0x1F68` = 1200 and a special case cutting the max to 1024 for PCI vendor `0x104A`.
The D3D9 enumeration loop (`0x50841D`: `GetAdapterModeCount` / `EnumAdapterModes` /
`CheckDeviceType`, bpp ≥ 16) skips any mode wider **or** taller than that max (read only at
`0x5084BC`). The modes form a linked list off the adapter object (`[0x78E374]`: count `+0x120`,
head `+0x124`; node `+8` w, `+0xC` h, `+0x14` fmt, `+0x18` refresh, `+0x20` next).

So on a 1920×1080 monitor the list stopped at 1600×900 (30 entries: 15 sizes × 60/144 Hz — it
looked like a 30-entry cap but is not), `-width 1920 -height 1080` was rejected **for both
fullscreen and windowed** (`*ERROR* ... passed on the command line is invalid!`), and the race
fell back to **`** Using default screen resolution: 800 x 600`** — stretched over the screen.
**Fix:** `GvrIOShim` `patch_mode_limit()` rewrites that immediate to 4096 in memory before
`main()` (bytes verified; `GVRIOSHIM_NO_MODE_PATCH=1` skips; `NASCAR_GVR.exe` unchanged on disk).
Result: `Using windowed screen resolution 1920 x 1080 from the command line`,
`Selected VideoMode: 32`. The shim's shell-only overrides (IO board, pedals, keyboard steering)
are now gated to `AMPlayer.exe`; in the race every GvrIO message passes through untouched.

### 13.2 `nascar_settings.ini` defaults explained the "small window"

The launcher appends `[Display]`'s `-windowed|-fullscreen -width W -height H` to every race the
shell starts (registry `cmd`). The shipped default is 1280×720 windowed — hence a small window
while the shell was full screen. Set Width/Height to the desktop size.

### 13.3 Launcher: borderless, always-on-top, taskbar (`src\NascarLaunch`)

One 250 ms loop while the launcher waits on the shell (`WaitForExit=true`):
* `[Display] Borderless=true` — the race runs `-windowed`; its window's caption and frame are
  stripped and it is placed over its monitor (overrides `Fullscreen`; Width/Height 0 = screen).
* The shell window is **`WS_EX_TOPMOST`** (seen in the log on every start) — that swallowed
  alt-tab. Demoted with `SetWindowPos(HWND_NOTOPMOST, …, SWP_NOACTIVATE)`; the race window too
  unless in exclusive fullscreen (Direct3D owns the flag there).
* The shell hides the taskbar; it is re-shown whenever neither `AMPlayer.exe` nor
  `NASCAR_GVR.exe` is the foreground window.

### 13.4 The 15 s "Please Wait..." and its loading bar

Not loading: startup step 4 sets `ioBoardDelay = presentation.realTime + 15.0` and step 8 waits
for it before reading the IO-board version (time for a real Nytric board to boot). The bar is
cosmetic (`scaleFactor *= frameCounter / 750` while the `Checks` slide is up). Nothing loads
meanwhile — the attract presentation is only loaded after the checks. On the cabinet the shell
autostarts at Windows boot, so the wait gave the USB board time to enumerate before its firmware
version was read (reading too early returns 0 → a false "GVRIO board" error). **Done
2026-10-04:** `Shell.am` `Behavior95` `ioBoardDelay = presentation.realTime + 15.0;` →
`+ 0.5;` via `Tools\Edit-AmScript.py` (record `0x28867`, compressed size kept; previous file
`Shell.am.pre-iobdelay`, pristine `Shell.am.oem`). The 0.5 s re-check in step 8 is unchanged.
User-confirmed: startup is visibly faster. Safe on a real cabinet too: the board-PRESENCE check
(step 7, `IOBoardMajorVersion == 0` → hard stop, no retry) runs *before* this delay, so the 15 s
never protected against a late board; the delayed step-8 version check re-checks every 0.5 s on
its own. For a cabinet that autostarts at boot, delay the launcher instead.

---

## 14. Gamepad support — and how the race really takes the cabinet's input (2026-10-04, night)

Goal: Xbox and PlayStation pads in the front end and the race, with NFS Underground's layout.
Everything below was read out of `NASCAR_GVR.exe` (Ghidra) and `GvrIO_oem.dll` (capstone); the
pad code is `src\GvrIOShim\GvrIOPad.cpp`.

### 14.1 The race drives from GvrIO, not from its `.CTL` controller lines

* `NASCAR_GVR.exe` calls `DirectInput8Create` exactly once (`0x45bc33`, in
  `HWInput::Setup`, `Source\Pc\Input\hwinput.cpp`) and creates **only the keyboard**
  (`0x457410`: `GUID_SysKeyboard`, `c_dfDIKeyboard`, device type `0x13`). `c_dfDIJoystick`
  (`0x6f2684`) is linked but **referenced nowhere**. So the `.CTL` "controller 1" bindings
  (`Accelerate (1,4)` etc.) are never read on this build — correcting §5b.
* `HWInput::Setup` then calls `cGvrIO::Initialize(5 /*Steering*/, hwnd, callback 0x456f80)`.
  Every frame `HWInput::Update` (`0x45be20`) → `FUN_0045c650` sends GvrIO msg `(1, 1)` (the
  pump: GvrIO polls its devices **and its own keyboard** and dispatches events through the
  callback, all inside that call), then msg **5** for axes 0/1/2 and msg **`0x42`** with the
  names `Shifter1`..`Shifter4`.
* Axis values: `v / 255` (`0x744dc0`), steering doubled and **signed** (so about −127..127 is
  full lock; mode `[cfg+0x62b8]` 0 squares it), gas/brake 0..255. They land at
  `0x91a654/58/5c` with "nonzero" flags `0x91a660/61/62`.
* The vehicle-input function at **`0x45a420`** (not a Ghidra function; it starts right after
  the `ret 4` at `0x45a41d`) takes those GvrIO values **whenever they are nonzero**, else ramps
  the digital cabinet buttons: **Forward = gas, Back = brake, Left/Right = steering**
  (`GvrIO.xml` keys `r c d g`). With manual transmission (`0x91a860`), the gear `shifty`
  (logged as `vehicleinput::shifty=%d`) is Shifter4→4, 3→3, 2→2, 1→1, none→3.

### 14.2 The race's GvrIO buttons (callback `0x456f80`, msg id `0xB`, event name at `+0xC`)

| Event (from the `GvrIO.xml` name) | State byte | What the race does |
|---|---|---|
| `onGvrForward/Back/Left/RightButtonDown/Up` | `0x91a663..66` | digital gas / brake / steer (14.1) |
| `onGvrViewButtonDown/Up` | `0x91a667` | rising edge → `FUN_00419130("Drivable")`: next driving camera |
| `onGvrLookBack(2)ButtonDown/Up` | `0x91a668` | rear view while held (`[esi+0x3c] = 1`) |
| `onGvrStartButtonDown/Up` | `0x91a669` | `GVRRestartRunning` (`0x485f40`): car put back on the track at the nearest waypoint, with a cool-down and a restart counter |
| `onGvrNOSButtonDown/Up` | `0x91a66a` | (re)starts vehicle sound slot 61 every frame while held (`FUN_004f7710(…, 0x3d, …)`, `eax = 1`) and **never stops it**. Tried as the horn on 2026-10-04: one press and it sounded forever. NASCAR's `GvrIO.xml` has no `NOS` entry, so on the cabinet this is dead code — leave it. The working horn is the engine's `.CTL` `Control - Horn` (14.4). |
| `onGvrMusicButtonDown/Up` | `0x91a66f` | next song (`0x4a83c0`) |
| `onGvrMotionDisableButtonDown/Up` | `0x91a670` | motion-seat toggle — irrelevant off-cabinet |
| msg `0x42` `Shifter1..4` | `0x91a66b..6e` | the 4-position H-shifter (manual only) |

**Four-button abort:** `FUN_004829b0` ends the race ("`Player initiated ABORT!`",
`DAT_0078e6e0 = 1`) when **View + LookBack + Start + Music** are held together, single player
only, and only if `HKLM\Software\GlobalVR\NASCAR\FourButtonAbort` is nonzero
(`FUN_004818c0`; the installer and the OEM `NASCAR.reg` set `1`; Operator → Game Settings can
change it). The race's `-noabort` switch does **not** disable it: the option table at
`0x762e34` (`{name, id}`, 49 entries, parsed at `0x45d3f0` into flag bytes at `0x91c710+id`)
maps `noabort` → id `0x22` → `0x91c732`, which only stops the **checkpoint timer** from ending
the race (`local_4 = 999.9` in the same function).

### 14.3 GvrIO's keyboard is the cleanest place to inject buttons

* `cGvrIO::Initialize` (`0x10002650`) is linear — no early exit without a board: it loads
  `GvrIO.xml` from `HKLM\SOFTWARE\Gvr\Plus\2.0\Cabinet\GameRoot` + `Config\`, creates the board
  objects, then the keyboard (`0x10001c50`: its own `DirectInput8Create` with
  `IID_IDirectInput8A`, `CreateDevice(GUID_SysKeyboard)`, `c_dfDIKeyboard`), loads plug-ins,
  registers the callback and starts its worker.
* The pump polls the keyboard with **`GetDeviceState(256)`** (`0x10001b60`) and turns each
  pressed scan code into a letter with **`MapVirtualKeyA(sc, MAPVK_VSC_TO_VK)`** (so it follows
  the keyboard layout). A held `<key char>` then behaves exactly like the board button of the
  same `name`: GvrIO sends `onGvr<Name>ButtonDown/Up` (id `0xB`, built at `0x100053ab`), the
  XML `event`, and adds the name to a held-names list (`0x1000ca18`, `0x10005140` /
  `0x100051a0`) that answers msg `0x42` ("is `<name>` held", case at `0x1000529a`).
* So a pad needs no event plumbing: OR its buttons into the 256-byte state of **GvrIO's own**
  keyboard object, and GvrIO raises every event itself, in both programs.
* **GvrIO's own selection** (`Gvr.SelectionX` = msg `(2, 0xF)`, answer via the pointer at `+0x10`;
  `SelectionRangeX` = msg `(1, 0x13, n)`; event `onSelectionChanged`) moves one step per *press*
  of `VK_LEFT` / `VK_RIGHT` (`0x10005591`: clamp to `[0, range-1]`, then callbacks `0x15`, `5`
  and `0xB "onSelectionChanged"`). Transmission select uses it instead of `Gvr.AxisX`, so a pad
  must press those keys. Through `MapVirtualKeyA` the real arrow keys' DirectInput codes
  `0xCB`/`0xCD` map to **no** virtual key; `0x4B`/`0x4D` (the numpad 4/6 position) give
  `VK_LEFT`/`VK_RIGHT` — on a keyboard it is numpad 4/6 that changes the transmission choice.
* The race reads **its own** DirectInput keyboard the same way (`GetDeviceState`, `0x457630`):
  the `.CTL` keyboard bindings and the numpad Swingman (chase) camera. Engine controls with no
  cabinet button can only be reached there.

### 14.4 Implementation (`GvrIO.dll` shim, deployed to `Game\` and `Shell\bin\`)

* `GvrIOPad.cpp` (new), wired from `GvrIOShim.cpp`. The pad reader is NFSU's `GvrInputEmu`
  ported: XInput (any of the 4 user slots) and DS4 raw HID (gamepad collection, USB or
  Bluetooth, feature `0x02` for the full Bluetooth report). Pad discovery runs on a background
  thread, so the game threads never pay for an empty XInput slot (NFSU's 100%-CPU trap) or a
  HID scan; unplugging and replugging is picked up.
* **Buttons:** `cGvrIO::Initialize` (already overridden) first redirects `GvrIO_oem.dll`'s
  `DINPUT8!DirectInput8Create` import, to learn which `IDirectInput8A` is GvrIO's. Then the
  `CreateDevice` (slot 3) and `GetDeviceState` (slot 9) entries of DirectInput's **shared**
  vtables are redirected to hooks that act only for GvrIO's objects; every other object passes
  straight through. After a successful `GetDeviceState` on GvrIO's keyboard the mapped cabinet
  letters get `0x80`, using `MapVirtualKeyA(VK, MAPVK_VK_TO_VSC)` so AZERTY works. A failed read
  (window not in front) is left alone.
  **Trap (first build):** giving the objects *private vtable copies* (swapping the object's
  vtable pointer) broke everything — `dinput8` validates `this` against its own vtables, so
  `CreateDevice` failed, GvrIO had no keyboard at all, and even the physical cabinet keys died.
  Found by reading the live shell's memory: `GvrIO_oem+0xC160` → object `+0x4C` (keyboard) and
  `+0x50` (mouse) were both 0 while the `IDirectInput8` carried the copied vtable.
* **Menu selection:** in the front end, D-pad left/right also press `VK_LEFT`/`VK_RIGHT`, and so
  does the left stick held over half-way, so transmission select works like the cabinet wheel
  (stick right = Manual).
* **Axes:** msg 5 (device 1) is answered from the pad when the OEM reports no usable gas range
  (`real_pedals()`: msg `0x3A` axis 1, max < 40) — in the **race too** now (before, the race was
  pass-through). A real cabinet wheel always wins; out of focus the pad reads neutral. Menus
  only: D-pad left/right steer ±60 like the keyboard.
* **Gears:** sequential over the 4-position shifter — Square/Triangle step 1..4 and the shim
  holds `t y u i` for the current gear (race process only; every race starts in 1st; ignored in
  automatic).
* **Quit:** hold D-pad down for 1 s → `v l s m` held together → the four-button abort.
* **Race-only keys** (`inject_race_keys`, the same `GetDeviceState` hook on the race's *own*
  keyboard, raw scan codes): the horn holds **J** — `Game\Save\GvrSinglePlayer\GvrSinglePlayer.CTL`
  now binds `Control - Horn="(0, 36)"` (OEM `(0, 89)` = unbound; `.CTL.oem` kept), so the
  keyboard's J is the horn too (**not H**: the engine reads H itself as its "cycle HUD mode"
  hotkey — normal / hidden / "Internal Pkts" debug overlay, `0x91ac20+0x23` at `0x4785d7`; it
  also hard-codes F G M P S T X Z Home End F8 F11 F12 and the numpad); the **right stick** holds numpad **4/6** (turn) and **8/2**
  (up/down) for the Swingman camera, over half-way. The engine's horn starts on press and stops
  on release. The first build's `GvrIO.xml` `NOS` line was removed again (`GvrIO.xml` is the OEM
  file).
* **Map:** `nascar_settings.ini` `[Controller]` (race) and `[Frontend]` (menus), with NFSU's
  syntax and button names; defaults in `map_defaults()`. `GVRIOSHIM_NO_PAD=1` disables all of
  it. Previous shim kept as `GvrIO.dll.pre-pad`. Exports unchanged (11).

### 14.5 NFSU layout → NASCAR

| Pad | NFSU race | NASCAR race | NFSU menus | NASCAR menus |
|---|---|---|---|---|
| Left stick | steering | steering | menu wheel | car / track select (wheel) |
| R2 / RT | gas | gas | accept (name entry) | gas pedal = select |
| L2 / LT | brake | brake | backspace | — |
| Cross / A | e-brake, confirm, skip intro | **— (no e-brake on this cabinet)** | select | select (Start) |
| Circle / B | nitrous | **horn** (the engine's Horn control, J) | exit | back (Look Back) |
| Right stick | — | **chase-camera turn** (numpad 4/6/8/2) | — | — |
| Square / X | shift up | shift up (manual, 4 gears) | — | — |
| Triangle / Y | shift down | shift down | — | — |
| L1 / LB | look back | look back | — | — |
| R1 / RB | camera | camera (View) | — | — |
| Options / Menu | start / reset car | Start = put the car back on track | operator | operator |
| D-pad up | — | — | nav up | up (View) |
| D-pad down | quit (prompt) | **quit: hold 1 s** (four-button abort) | nav down | down (Music) |
| D-pad left / right | — / music | — / music | nav | left / right (+ steer the selectors) |
| R3 | — | — | card | **— (no card reader)** |

### 14.6 Status

**User-confirmed 2026-10-04** with a PS4 DualShock 4 over Bluetooth (raw HID, report `0x11`,
547 bytes): menus, driver / transmission (Manual included) / track select, steering, gas and
brake, the buttons, manual shifting, the horn and the right-stick camera. Not separately
reported: hold-to-quit. Not tried: an Xbox pad (its XInput path is NFSU's, unchanged).
`nascar_settings.ini` `[Debug] Log=true` (read when each program starts; or the environment
variable `GVRIOSHIM_LOG=1`) writes `gvrioshim.log` next to each executable: hook installation, pad
found/lost, the map in use, and every change of the held cabinet and race keys.

**Horn — final (2026-10-05, user-confirmed):** the engine's `.CTL` `Control - Horn` makes **no sound** in this build (tested on H and J, keyboard and pad). The only horn is the cabinet **NOS** input: `GvrIO.xml` gets `<key char="j" name="NOS" …>` (J, because H is the engine's HUD-cycle hotkey), the pad's horn holds that key, and `GvrIOShim.cpp` `patch_horn_stop()` reroutes `0x4DA348` through a stub that stops vehicle sound 61 when the NOS flag drops — the sample loops and the race never stopped it (flag watched live: it does return to 0). Stop recipe copied from the race's checkpoint-warning code at `0x482BD5`: slot = owner+`0xA80` (or `+0x10CC` if byte owner+`0xC8` is 0) + `0xE8` + id×`0x14`; handle pair → `ebx`/`esi`; call `0x4A73F0`; clear the active byte owner+(id×9+`0x34`)×4. `GVRIOSHIM_NO_HORN_PATCH=1` skips it. `Control - Horn` is back to the OEM `(0, 89)`. This supersedes the §14.4 "race-only keys: horn holds J" bullet.

---

## 15. A private registry - the game no longer uses the Windows registry (2026-10-05)

Goal: no missing-registry start failures, no administrator rights, and no collision with NFS
Underground (needed for a cabinet that boots either game). `HKLM\SOFTWARE\gvr\Plus\1.1\Cabinet`
is genuinely shared: it held NASCAR's `PlusSchemaPath` next to NFSU's `PatchDownloadPath`.

**Who reads what** (value names found in the binaries): the shell scripts (`launchFolder`,
`attractFolder`, `selectFolder`, names), `NASCARPlugIn.dll` (resolution, `FourButtonAbort`,
`UpTime*`, `Prefix`/`Suffix`), `NASCAR_GVR.exe` (resolution, `FourButtonAbort`,
`StallMonitorTest`, `cmd`, `tracefilenum`), `GvrParseXml.dll`/GvrIO (`GameRoot`), the GvrPlus
DLLs (`PlusSchemaPath`, `PublicKeyPath`, `PromotionPath`, `MaxObjectLoadCount`),
`GvrMusicPlugIn.dll` (`MusicVolume`). Only `Version`/`Build`/`CommVersion`/`wtf` are pure checks,
so the values are *answered*, not deleted.

**Design** (`src\GvrIOShim\GvrIOReg.cpp`, in the GvrIO shim): the advapi32 `Reg*` exports
(hot-patch prologue `mov edi,edi` + 5×`CC`) are redirected; any key under `HKLM\SOFTWARE\GlobalVR`
or `HKLM\SOFTWARE\gvr` (either view, opened directly or step by step - parent paths via
`NtQueryKey`) becomes a private handle (odd value) served from `<install>\nascar_registry.ini`
(one section per key, values quoted, DWORDs as `dword:xxxxxxxx`). Folder values are always
computed from the install folder; anything missing falls back to the OEM defaults (`DEFAULTS`),
`UpTime*` to "now". First run copies the real registry's values in (never overwriting).
Every other key goes to the real registry. `GVRIOSHIM_NO_PRIVATE_REGISTRY=1` turns it off.

**Load order:** the race imports the shim (DllMain runs before `main`); the front end reads
`GameRoot` *before* loading plug-ins, so `NascarLaunch` starts `AMPlayer.exe` suspended and queues
`LoadLibraryA(Shell\bin\GvrIO.dll)` as an APC on its main thread (runs during loader init). The
launcher now writes its ini-driven values (`FullScreen*`, `MusicVolume`, `SimpleAttract`, `cmd`)
into `nascar_registry.ini` - no elevation, the old HKLM self-repair is gone.

**Verified** in a scratch test (A and W calls, size queries, set/delete, step-by-step opens,
unrelated keys untouched) and in a real session (user, 2026-10-05): menus and a full race; the
store got the shell's `UpTime*`/`CommandLine` and the race's `tracefilenum=34` while the real
registry stayed at 33. Not caused by it: `Result_*` stay 0 (also in the real registry before), and
`AMPlayer` exiting with 0xC0000005 after a session (seen in four earlier sessions the same day).

**Done the same day.** `Install-NASCAR-GVR-Portable.ps1` no longer writes these HKLM keys. It
only creates `nascar_registry.ini` (`FullScreenWidth`/`Height`) if it is missing. The hook engine
moved to a shared library, `GIT\src\GvrPrivReg`, and `GvrIOReg.cpp` now holds only NASCAR's roots
and defaults. NFSU uses the same library in `GVRInputRaw.dll` (`nfsu_registry.ini`,
`GIT\src\GvrInputEmu\NfsuPrivReg.cpp`), with `noSeed` set because the real `gvr` tree holds
NASCAR's values. See `GAME_ONLY_INSTALL_PLAN.md`, 2026-10-05.


---

## 16. A fresh install: the operator's first-time setup (2026-10-05)

Found by installing from the release folder onto a new folder and comparing it with the
working install.

* **Symptom 1: "Calibrate the accelerator and brake" on the first start.** It disappeared on
  the second start.
* **Symptom 2: free play was off,** and several operator options were on.
* **Cause: `NASCARPlugIn`'s `Operator_FirstTime_Init` ran.** It runs when the database
  `GlobalVariable` `gOperatorFirstTimeInitilized` (the misspelling is the game's own) is `0`,
  which is what the OEM data ships. It then does all of the following:
  * writes `FirstTimeStartup = 1` to the registry. `Shell.am` shows `PedalCalibrate` whenever
    `FirstTimeStartup == 1`, whatever the pedal ranges are, and then writes `0`. That is why the
    screen appeared only once.
  * runs `Operator_ResetFactoryDefaults`, which overwrites `CabinetConfiguration_NAS1`:
    `FreePlay 0`, the options at indexes 13-14, 19-20, 22-23 and 25-27 set to `1`,
    `TracksDisabled 65535`, `AttractVolume 90`;
  * resets the pricing model and calls `Operator_SetTimeZone`.
* **Fix:**
  * `Build-NascarSqliteDb.py` provisions `gOperatorFirstTimeInitilized = 1`, so the routine never
    runs. That matches the working install, where the flag was already 1.
  * The private registry gains a `FirstTimeStartup = 0` default.
* **The shim's pedal answer was correct all along.** The shim log showed the OEM `GvrIO`
  answering nothing for axes 1 and 2 (range -1..-1), and the shim reporting 0..255. Its range
  test was still tightened, to the script's own rule (max - min < 40).
