# NASCAR Team Racing (GlobalVR) — reverse-engineering findings

How the NASCAR cabinet software works inside, and what it takes to run it on a normal PC.
Everything below was verified against the extracted media on 2026-08-12, unless marked
*(inferred)*.

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
| Extractor | `Tools\unshield.exe` handles it directly |
| Author string in `setup.inx` | `C:\Projects\NASCAR\SETUP\Installer\Script Files\Setup.dbg` |

`setup.inx` still contains the `RUNTIMEOEMREV` string. This is GlobalVR's XP-Embedded gate, so
the OEM installer refuses to run on ordinary Windows. **The game executable itself does not
check it**: `NASCAR_GVR.exe` holds no such string. This is why a file-copy installer works.

NASCAR's value, read from the cabinet's own SYSTEM hive in the recovery image:

```
RUNTIMEOEMREV     = NASCAR,XP Embedded,HW Rev 945-G31,08252008
RUNTIMEOEMVERSION = 1.5.0.04
```

The gate needs both values. `GVRSETUP.INI [MINIMUMOS]` checks the product name, the date and
the version. The installer writes them with `-SetOemRev`. The full registry reference is in
[`NASCAR_REGISTRY.md`](NASCAR_REGISTRY.md). It includes the cabinet OS layer that the discs
never ship.

### Recovery disc — `RECOVERY DISC ISO EXTRACTED\`

A bootable WinPE 2.0 restore disc. It is not a game disc.

* `BOOTMGR` + `BOOT\BCD` + `SOURCES\BOOT.WIM` (89 MB): the WinPE that runs at boot.
* `GVR\GVRPESHL.INI` describes the restore job:
  ```ini
  [Global VR SRD]
  Product=NASCAR
  HwRev=945-G31          ; Intel 945 board / G31 chipset cabinet revision
  ReleaseDate=08/26/2008
  Volume=C
  VolumeLabel=NASCAR
  SettleSeconds=45
  ```
* `GVR\TOOLS\DISKPART.TXT` is **destructive**: `select disk 0` / `clean` / create+format C:.
* `GVR\NASCAR-20080826-1.GVR` (425 MB) is a plain Microsoft WIM, despite the extension. It
  has the `MSWIM` magic, format version 1.13. 7-Zip, `wimlib` and DISM all read it.

WIM metadata: image name `NASCAR-20080826-1`, `PRODUCTSUITE=EmbeddedNT`,
Windows 5.1.2600 SP build 2180 (XP Embedded SP2), 820 dirs / 4,684 files / 1.03 GB.

The WIM holds the cabinet's C: drive before the game is installed:

| Path | What |
|---|---|
| `!Drivers\945`, `G31`, `LAN`, `LAN8139`, `NVIDIA\177.83`, `NVIDIA\94.24` | Board, GPU and network card drivers |
| `!Drivers\HASP\hdd32.exe` | The dongle driver: Aladdin HASP device driver (4.4 MB) |
| `WINDOWS\Microsoft.NET\Framework\v1.0.3705`, `v1.1.4322` | .NET, installed state |
| `Program Files\Microsoft SQL Server\MSSQL` | MSDE 2000 SP3 (`sqlservr.exe` 8.00.760). System databases only. |
| `Program Files\SQLXML 3.0` | Required by the GvrPlus stack |
| `Program Files\DivX`, `C-Media`, `Realtek`, `Intel` | Codec, audio and network card packages |
| `GVR\GVR_Tools` | `SetVolume`, `SetVRes`, `HideTaskBar`, `ShowOemRev`, `CDS`, `fbreseal`, `finishos`, … |
| `GVR\DIAG` | `SimpleHIDWrite.exe`, Nytric `MultiUSBIOTest.exe`: I/O board diagnostics |

So, are the prerequisites inside the .GVR? Yes, but as installed state, not as redistributable
installers. The only real installers inside are device drivers (NVIDIA, HASP, Intel LAN, C-Media).

---

## 2. Payload → install layout

The cabinet's layout is certain. The OEM registry files and `GVRSETUP.INI` pin it down, so
nothing is guessed. The table below shows what the OEM installer would have done.

The portable installer does **not** reproduce these paths. It nests everything under a folder
you choose and repoints the registry to match, as described in the README.

| Cabinet file group | Destination | Evidence |
|---|---|---|
| `NASCAR\Game` | `C:\NASCAR\Game` | `GVRSETUP.INI [GAMEINSTALLED] File=C:\NASCAR\Game\NASCAR_GVR.exe`, `NASCAR_Shell.reg launchFolder` |
| `NASCAR\Shell` | `C:\NASCAR\Shell` | `NASCAR_Shell.reg attractFolder/selectFolder`, `HerculesCab.reg Prog04 path` |
| `Hercules` | `C:\hercules` | `HerculesCab.reg` `path="c:\\hercules"` |
| `GVRPLUS` | `C:\GvrPlus` | `Schema.reg` `C:\GvrPlus\4\...`, and the working folder of the `GVRSETUP.INI` database step |
| `GVR\GVR_Tools` | `C:\GVR\GVR_Tools` | `GVRSETUP.INI` `C:\GVR\GVR_Tools\SetVolume.exe` |
| `WINDOWS\system32`, `Fonts`, `Cursors` | `%SystemRoot%\…` | File group naming, plus the `NASCAR.reg` cursor paths |

### Game tree size breakdown

```
Audio      3,193 files  357.6 MB      Options       73 files    7.7 MB
GameData     913 files  189.9 MB      Telemetry     57 files    9.7 MB
GameSpy        2 files    6.2 MB      Save          25 files    0.1 MB
Replays        1 file     5.3 MB      Animations     4 files    1.2 MB
```

---

## 3. The game binary

`NASCAR_GVR.exe`: 3.6 MB, PE32, built from `\Projects\Nascar\Dev\Source\Pc\…`.

**This is EA's NASCAR engine** inside a GVR arcade shim. It loads `.MAS` archives
(`EALoad.mas`, `SpecialFX.mas`), `.gdb` season databases and `.sci`/`.tbc`/`.svm` sim files.
It also uses Miles Sound System, `Mss32.dll`, and Bink, `binkw32.dll`. This is the NASCAR
Thunder / SimRacing lineage. Knowledge of the retail engine carries over directly, such as the
PLR/ini tuning files and `.mas` tooling.

### Imports (verified)

```
NASCAR_GVR.exe : WINMM, DSOUND, KERNEL32, USER32, GDI32, ADVAPI32, SHELL32,
                 binkw32, d3d9, DINPUT8, mss32, GvrIO, MSVCP71, WS2_32
AMPlayer.exe   : OPENGL32, GLU32, DDRAW, MSACM32, WININET, urlmon, RPCRT4,
                 MSVCR71, MSVCP71, COMCTL32, …          (Anark Media Player 3.0)
GvrIO.dll      : USBIOExtreme, DINPUT8, HID, SETUPAPI, ole32, OLEAUT32, MSVCR71
motionLib.dll  : cbw32 (Measurement Computing InstaCal — motion base only)
```

**Key result: the game executable has no SQL, no .NET, no PLUSDE and no GvrPlus dependency.**
The database exists for the shell's accounting and leaderboards. So a game-only install does
not need MSDE at all.

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

`-cmdfile` reads arguments from a text file. The shipped `Game\cmdline.txt` is an example of
that format, with `//` comments. It is left over from development.

### Content actually shipped

Tracks: `BRISTOL`, `DAYTONA`, `INDIANAPOLIS`, `LOWES`, `PHOENIX`, `TALLADEGA`
(+ `Common` and `HAT` support folders). The binary knows 24 track names. The rest are
retail-engine leftovers with no data on the disc.

Series: `2006NEXTEL`, `2005NEXTEL`, `2004NEXTEL`, `2004NNS`, `2004CTS`.

---

## 4. GvrIO — the hardware layer (and the plug-in ABI)

`GvrIO.dll` is GlobalVR's cabinet input/output layer. It is an 11-export C++ class plus a
**plug-in loader**.

```
?Initialize@cGvrIO@GvrIO@Zeus@Gvr@@…(GVR_DEVICE_TYPES, HWND, callback)
?MessageRegisterCallback@cGvrIO@…(GVR_DEVICE_TYPES, callback)
?GvrIOMessageSend@GvrIO@Zeus@Gvr@@YAPAXPAUsMessage@123@@Z
??0/??1/??4 cGvrIO, GvrIOLogger  (ctor/dtor/assign, LogPrintf, LogErrorPrintf)
```

Behaviour recovered from its strings:

1. Reads `GameRoot` from `HKLM\SOFTWARE\Gvr\Plus\2.0\Cabinet`.
2. Loads `file://<GameRoot>\Config\GvrIO.xml`, the button and key map. It ships in `Game\config\`.
3. Loads named plug-ins as `<name>.dll` and resolves `Initialize` and `MessageCallback`
   (and `Finalize`). Known names in the string table: `Dongle`, `CardDispenser`.
4. Device classes: `Steering`, `Joystick`, `Trackball`, `Nytric`, `GvrIOmini`, `ComboButton`.
5. Talks to the Nytric USB I/O board through `USBIOExtreme.dll`
   (`CUSBIO_Init/Update/Close`, `SetInputMode`, `SetOutputValue`). Talks to HID devices through
   `SETUPAPI`/`VID_%04hx&PID_%04hx&MI_%02d`.

The shipped plug-ins in `Shell\bin` that follow this ABI are `Dongle.dll`, `Steering.dll` and
`Motion.dll`. Each exports exactly `Initialize`, `MessageCallback`, `Finalize`.

### Keyboard fallbacks (from `Game\config\GvrIO.xml`)

| Key | Function | Key | Function |
|---|---|---|---|
| `s` | Start | `q` | Quit |
| `l` | Look back | `v` | View / camera |
| `m` | Music | `o` | Operator menu |
| `t` `y` `u` `i` | Shifter 1 to 4 | `r` `c` `d` `g` | Forward / Back / Left / Right (menu nav) |
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

So the game reads four fields from the key: game name, version, region code and cabinet type.
A missing key or a mismatch is fatal. The arg table has no `-nodongle` switch.

`Shell\bin\Dongle.dll` is the reader. It statically imports only `KERNEL32` + `MSVCR71` and
resolves everything else at runtime. Its string table names `HASPUT16.DLL`, `wtsapi32/wfapi`
(Terminal Server detection), `UTRegister/UTUnRegister` and
`System\CurrentControlSet\Control\ProductOptions`. That is the classic Aladdin HASP runtime
pattern. It matches `!Drivers\HASP\hdd32.exe` in the recovery image.

Attack point, not yet implemented: replace `Dongle.dll` with a shim that exports
`Initialize` / `MessageCallback` / `Finalize`. The shim answers the dongle query with
`game=NASCAR`, a valid version, region and cabinet type. The rule for such a shim: **forward
the whole ABI to the original DLL, renamed `Dongle_oem.dll`, and override only the calls you
mean to override.** Stubbing everything leaves cabinet probes unanswered, which shows up as
long idle hangs.

Still unknown: the `sMessage` layout and the message IDs used for the dongle query. That needs
a Ghidra pass on `NASCAR_GVR.exe` + `Dongle.dll`.

### Observed startup behaviour (2026-08-12, Win11 x64 host, no registry keys, not elevated)

Launched `NASCAR_GVR.exe -mode single -track DAYTONA -windowed -width 800 -height 600`
from the extracted tree, with the GvrIO + MSVC 7.1 DLLs staged beside it. Result:

* The process starts with 7 threads and a ~15 MB working set. It **stays alive past 120 s**.
* It creates `LOG\LOADTIME.txt` (0 bytes). So `main()` runs and the engine's logging opens.
* It never creates a window and never writes any other file.
* The main thread parks in `ExecutionDelay`, a `Sleep` retry loop.

That is a probe/retry stall, not a crash. It is the typical sign of cabinet hardware probes
going unanswered. Ranked suspects:
1. `Dongle.dll` retrying the HASP key, with no driver and no key present.
2. `GvrIO` failing to resolve `GameRoot`. The registry keys were absent in this test. The
   installer writes them, so this needs a new test after a real elevated install.
3. The Nytric/`USBIOExtreme` board enumeration.

---

## 5b. Controllers — the retail EA input stack is intact

> **Corrected 2026-10-04 (§14):** on this GVR build the race opens no DirectInput joystick,
> only a keyboard. So the `.CTL` "controller 1" lines below are never read. The cabinet's
> wheel and pedals reach the race as GvrIO msg 5 axes, and its buttons as GvrIO events. The
> Xbox / PlayStation pad support plugs in there too. The `.CTL` keyboard lines still apply.

NASCAR keeps EA's data-driven controls system. An early claim is wrong and struck out:
~~Driving input goes through the game's own `DINPUT8.dll` usage, not through GvrIO~~. GvrIO
carries the cabinet buttons: start, view, music, shifter and coin. It also carries the analog
wheel and pedal axes (§14.1).

Config files, plain text, in `Game\Save\GvrSinglePlayer\`:

| File | Contents |
|---|---|
| `GvrSinglePlayer.CTL` (22 KB) | `Player Controls File … Copyright (c) 2003-2004 Electronic Arts Inc.`. Every binding, dead zone, sensitivity and FFB parameter. |
| `GvrSinglePlayer.PLR` (28 KB) | Player and driving preferences (`Speed Sensitive Steering`, `Opposite Lock`, cockpit options, …) |
| `GvrSinglePlayer.gal`, `tempGarage.svm`, `Settings\<series>\<track>\*.svm` | Garage setups per track |

Binding syntax is `"(device, control)"`:

* device `0` = keyboard. The control is a DirectInput scan code, for example `200` = `DIK_UP`,
  `208` = `DIK_DOWN`, `16` = `DIK_Q`.
* device `1`+ = a DirectInput game device. The control is an axis or button index.

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
The section also has per-axis `Dead Zone` / `Sensitivity` / `Center` entries. A full FFB block
follows, with `FFB Gain`, `FFB Effects Level` and spring coefficient/saturation per joystick axis.

This paragraph and the next are the original expectation, superseded by the 2026-10-04
correction at the top of this section. Consequence: gamepad and wheel support here is expected
to be a configuration exercise, not a DLL shim. Rebind to the pad's axes and set
`FFB Device Type="3"` for a rumble pad. This is unverified only because the dongle stall (§5)
prevents reaching a race.

If the GVR shim overrides the CTL, the fallback is `Shell\bin\Steering.dll`. It is a GvrIO
plug-in with the same `Initialize`/`MessageCallback`/`Finalize` ABI as `Dongle.dll`, and can be
replaced the same way.

## 5c. The database — SQLite, built and verified

`NASCAR_GVR.exe` itself has no SQL/.NET/PLUSDE dependency (§3), so **racing needs no database
at all**. The database exists for the Anark shell and its `GvrPlusDEPlugin`
accounting/leaderboard path. In other words, it serves the full cabinet experience.

This is now built on SQLite, so no MSDE / SQL Server is ever installed. `PLUSDE.dll`'s 10
`System.Data.SqlClient` typerefs are swapped to a `GvrSqlite` provider that P/Invokes
`sqlite3.dll`. `game.db` is generated from the decrypted OEM schema: 106 tables, 366 seed rows,
operator pricing and 360 leaderboard rows.

Build it with `Tools\Build-SqliteBackend.ps1` and install with
`Install-NASCAR-GVR-Portable.ps1`. Full design, provisioning rationale, verification results
and known boundaries: [`NASCAR_SQLITE.md`](NASCAR_SQLITE.md).

Two NASCAR-specific notes:

* GlobalVR reused the GvrPlus AES key/IV across titles. So the same `.enc` decryption works
  unchanged on NASCAR's 2008 media.
* Patched binaries from other GVR titles do not transfer. NASCAR ships its own `PLUSDE.dll`
  build (1,810,432 B). It also calls `ExecuteScalar`, `CommandTimeout`, `SqlError.Message` and
  `SqlException.Message`, and the provider implements all four.

The alternative route, for the genuine article: MSDE 2000 with `PGA.msi` and `Nascar_db.exe`.
SQL Express can take the place of MSDE 2000 if the connection string in
`NASCARcabinetXml.enc` is re-encrypted with the same AES key/IV.

## 6. Registry map

The disc ships nine `.reg` files. The table shows what each one does, and whether the
game-only installer touches it:

| File | Contents | Used? |
|---|---|---|
| `NASCAR.reg` | `HKLM\SOFTWARE\GlobalVR\NASCAR` version/build/`FullScreenWidth=1360`/`Height=768`. Also a full HKCU cursor-scheme replacement. | Partly. Game keys yes. The cursor block only with `-InstallCabinetCursors`. |
| `NASCAR_Shell.reg` | Launch wiring: `launchName`/`launchFolder`/`attractName`/`selectName`/`cmd="-startPos 43 "`/`MusicVolume` | Yes, repointed at the chosen install root |
| `NASCAR_GvrIO.reg` | `HKLM\SOFTWARE\gvr\Plus\2.0\Cabinet\GameRoot` | Yes |
| `GvrShell.reg` | HKCU Anark Client 3.0 prefs (`DisableGL=1`) | Yes, only when the shell is installed |
| `Schema.reg` | GvrPlus 1.1 cabinet paths, `WebServerIP=192.168.1.39` | Only with `-InstallGvrPlus`. The dev web server IP is dropped. |
| `Startup.reg` | `Run\GVRHercules = c:\hercules\gvrboot.exe` | Never |
| `HerculesCab.reg` | Coin/dongle/stall/crash monitor stack + AMPlayer autostart | Never |
| `ConfigEventLog.reg` | Zeroes event-log retention on all three logs | Never |
| `tsunami.reg` | Tsunami motion base (`motionLib.dll`, `MotionType=4`) | Never. Needs `cbw32.dll`/InstaCal hardware. |

Note `FullScreenWidth=1360`/`FullScreenHeight=768`: **NASCAR takes its resolution from the
registry and the command line**, not from constants in the EXE. Widescreen needs no patching.

On x64 Windows the 32-bit game reads `HKLM\SOFTWARE\WOW6432Node\…`. The installer writes both
views.

The cabinet-OS half of the registry is documented separately in
[`NASCAR_REGISTRY.md`](NASCAR_REGISTRY.md). It covers the OEM-revision gate, autologon, the GVR
autostart entries and the Aladdin HASP driver services. It was read directly from the recovery
image's hives with `Tools\Dump-Hive.py`.

---

## 7. The shell and the database (context, not required for the game)

* The shell is Anark Media Player (`AMPlayer.exe` + `AKPlu*.dll` plug-ins). It runs
  `Shell\shell.am`, `NASCAR_Attract.am` and `NASCAR_Selection.am`. These are Anark scenes, not
  GlobalVR's UniverShell2 `.gvr` format.
* `Game\config\GvrShellPlugInList.xml` lists the shell's plug-ins: `ZeusIOPlugIn`,
  `HerculesPlugIn`, `GvrDiagnosticsPlugIn`, `NASCARPlugIn`, `GvrMusicPlugIn`,
  `GvrDirtyWordPlugIn`, `GvrPlusDEPlugin`, `GvrLinkingPlugIn`, `GvrMotionPlugIn`.
* On the cabinet, the database install first runs `msiexec /i DbSetup\PGA.msi /quiet`. That
  MSI is a VS.NET setup project, a .NET 1.1 assembly. Then `DbSetup\Nascar_db.exe` runs with
  working directory `C:\GvrPlus\4\schema\game`. It loads the `.tbl`/`.sql` files shipped there
  (`a+PricingConfiguration.tbl`, `NascarLeaderboard.sql`, `adminplus.sql`, …).
* The encrypted connection string lives at `C:\GvrPlus\4\schema\NASCARcabinetXml.enc`. It is
  AES-128 like the other GvrPlus `.enc` files, and can be re-encrypted with the same key/IV.

### GlobalVR platform DLLs on the NASCAR media

The GlobalVR platform DLLs ship in NASCAR's own 2008 builds. Anything patched, such as
`PLUSDE.dll`, is derived from these exact files:

| DLL | NASCAR |
|---|---|
| `PLUSDE.dll` | 1,810,432 B |
| `GVRStorageDevice.dll` / `DongleStorageDevice.dll` / `GVRSDEmulator.dll` / `GVRSCR28.dll` / `PCSCSCR2.dll` | present |
| `GVRInputRaw.dll` | 106,496 B |

Also on the NASCAR media: `akshasp.dll`, `haspms32.dll` (HASP), `MFC71.dll`, `msvcr70.dll`,
`UsbTrackerDll.dll`, `MdmXSdk.dll`, `csamsp.dll`, `GVR_Resources.dll`, `PATHMAN.EXE`.

---

## 8. Startup, solved (2026-09-30) — the three things that stopped the game

Verified on Win11 x64 with x32dbg (MCP, port 3032) and Ghidra. The EXE base 0x400000 equals
the live base, so every address here maps 1:1 in both tools.

### 8.1 Read the game's own log first

`main()` calls `FUN_004430e0()` before anything else. That call opens
**`Game\LOG\trace00N.txt`**. N increments per run, and `Config.ini` sets `LOGDIR=LOG\`. The
trace contains:

* the parsed command line (`ArgManager: CmdLine=<...>`, then one line per recognised arg)
* the dongle result (`DONGLE found: game=%s, version=%s, region=%s, cab=%s`)
* state banners `[ ENTER ] [ SETUP ] [ INIT ] [ RESTART ] [ POST ]` and the matching
  `**FATAL** Game failed the <X> state`
* `LOADTIME:` timings per init step, `== SystemInfo ==`, the chosen video mode
* every `SetError FATAL! : 'Could not locate ...'`
* `*** BADSTUFF: Unhandled Exception! ***` with ExpCode / ExpFlags / ExpAddress
* `~ fini ~` on a clean exit

The game only looked silent. The OEM tree ships no `LOG` folder and the engine will not create
one, so every write failed. `Install-Payload` now creates it. This trace is the single most
useful diagnostic in the project. Check it before reaching for a debugger.

`OutputDebugString` is not a useful channel. The only message the game ever emits is
`ACTIVATE`. Under a debugger it shows as DBG_PRINTEXCEPTION_C 0x40010006, otherwise through
DBWIN.

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

The code at 0x0048B8E0 builds the directory plus `*.CTL` and enumerates it. It calls
`FUN_004F9710` for the list, then `FUN_004F9BE0` for the first match:

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

If the enumeration is empty, the engine still stores NULL as the current entry. It calls the
path resolver anyway, and copies from the NULL it returns. The resolver:

```c
char* FUN_004f9a70(void)            /* object passed in EAX */
{
    piVar3 = *(int**)(*in_EAX + 8); /* the current entry */
    if (piVar3 == 0 || *piVar3 == 0) return 0;   /* <-- taken; returns NULL */
    ...
    sprintf((char*)(*in_EAX + 0x10), "%s%s", basedir /* entry+0x144 */, entry + 0x30);
```

So one missing or misnamed directory causes a silent, invisible startup crash. The
unhandled-exception filter `FUN_0045dc10` logs to the trace and calls `_exit(-1)`. No dialog
appears and no WER event is written.

Names the engine requires with spaces. They come from the cabinet's own directory table via
`unshield l data1.cab`, so they are authoritative, not guessed:

    2004 NNS   2005 NEXTEL Cup   Basic Settings   New Hampshire
    Pit Elements   Tab Backgrounds   UI Elements

**`unshield` rewrites DIRECTORY names when extracting**: spaces become underscores. FILE names
stay untouched, and `-R` disables the conversion. Repair with `Tools\Fix-GvrDirNames.ps1`.

Never blanket-replace underscores, because `END_OF_SEASON`, `MAIN_MENU`, `PP_SOT`,
`Career_NEXTEL` and similar names are genuine. Validation: the repaired tree matches 4780 of
4791 cabinet entries. The other 11 are `InstallGVR\*`, which is not deployed.

Other strings in the EXE confirm names with spaces:
`Options\Preview Images`, `GAMEDATA\VEHICLES\2004 NEXTEL CUP\FORD\17\17.VEH`,
`%s%s%s\%s Load%.2d.jpg`, `%s%s%s\%s SyncScreen.jpg`.

### 8.3 The HASP dongle gate — complete map

HASP is **statically linked** into `NASCAR_GVR.exe`, so there is no DLL to replace.
Record: 27 bytes at `0x00779CC0 + idx*0x1b`. `+0` is the present flag, `+1` the game name
(NUL at +7). `DAT_0077A290` is the index bound. It is 0 when nothing is attached, which still
admits index 0.

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

`main()` (`FUN_0045dc80`) keeps a local `bVar1 = true`. It clears it only when
`present && valid && stricmp(FUN_00672220(), "NASCAR") == 0`. While it is set, the whole
`INIT`, `RESTART` and play sequence is skipped. The game then shuts down having logged only
`**  FATAL  ** NO DONGLE DETECTED!`.

A 39-byte stub over `FUN_00672460` passes all three checks. See
`src\GvrIOShim\GvrIOShim.cpp :: patch_dongle_gate`. The stub writes 1 to `record[0]`, writes
`NASCAR` plus a NUL to `record[1..7]` and returns 1. The real function is 83 bytes
(0x00672460..0x006724B2, then `CC` padding), so nothing after it is disturbed.

The shim's DllMain applies the stub, and it runs before `main()`. So the executable is never
modified on disk, and every launcher behaves the same. Guard rails:

* The original 16 bytes (`8B 54 24 04 85 D2 7C 48 3B 15 90 A2 77 00 7F 40`) are checked first.
  A mismatch refuses the patch.
* The record address is computed from the real load base.
* `GVRIOSHIM_NO_DONGLE_PATCH=1` disables the patch for a cabinet with a genuine dongle.

Result in the trace: `DONGLE found: game=NASCAR, version=, region=, cab=`. Version, region and
cabinet type stay empty. This logs two non-fatal warnings (`game.cpp` 3582 / 3603).

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

**The ArgManager splits on whitespace and ignores quotes.** So a value containing a space
cannot be passed. `-series "2005 NEXTEL Cup"` arrives as `'"2005'` and is rejected ("not a
valid series name"). `2005 NEXTEL Cup.gdb` carries `Series Default Filename = true`, so leave
out `-series` and let it default.

Launch confirmed playable, verified with a full race at Daytona:

    NASCAR_GVR.exe -track Daytona -startPos 43

Tracks: `Bristol`, `Daytona`, `Indianapolis`, `Lowes`, `Phoenix`, `Talladega`.
Series `.gdb` files: `2004 CTS`, `2004 NEXTEL Cup`, `2004 NNS`, `2005 NEXTEL Cup`.
The OEM registry `cmd` is `-startPos 43 ` and the front end appends the track. So
`HKLM\SOFTWARE\GlobalVR\NASCAR` needs no `-track` of its own.

### 8.5 Asset gaps that are NOT installation faults

The game logs these as `SetError FATAL!` and races on anyway. None of them is in the cabinet
either, so they are gaps in the OEM content:

* `Daytona Load04.jpg`: only `Load01..03` ship.
* `2005-DAYTONA500.clf`: Daytona and Talladega ship no `.clf`. Bristol, Indianapolis, Lowes
  and Phoenix do.
* `POPUPQUIT0000.TGA`, `POPUPHEADER.TGA`, `VEHICLEDISPLAY.TGA`.

### 8.6 Notes on the tooling

* x32dbg's MCP server speaks JSON-RPC over `POST /mcp`. `GET /mcp` is an SSE stream and will
  hang a probe. `tools/list` returns 80 tools. `register_get` rejects the obvious parameter
  name, so use `eval_expression` with `esp` instead.
* x32dbg pauses on TLS callbacks and DLL entry points by default. So any run loop must skip
  stops that are not its own breakpoints.
* Breaking on `ntdll!KiUserExceptionDispatcher` and reading `[esp]` as `EXCEPTION_RECORD*`
  gives the code and address of every exception. `0x40010006` is only `OutputDebugString`.
* A first-chance stop whose EIP is a function prologue in a system DLL, with ECX equal to EIP,
  is a TLS-callback breakpoint, not a fault.
* `build.cmd` for the shim had lost the backslashes before `vswhere.exe` and `vcvars32.bat`.
  They had been written as literal 0x0B vertical tabs, which display as `Installerswhere.exe`
  and `Buildcvars32.bat`. This is the same class of trap as the `.def` `@`-truncation bug in
  section 5. The file is rewritten, and no other file in the tree contains a stray 0x0B.

---

## 9. The shell, and the shell-side dongle gate (2026-10-03)

These findings come from decompiling the managed plug-ins and from an end-to-end run of the real
cabinet path. They are not inferred. This section replaces the earlier vague note that "the shell
launches the game using the registry".

### 9.1 `AMPlayer.exe` *is* GvrShell

It is not a stock Anark player. Its own build paths are `c:\dev\zeus\shell\gvrshell\...`, and its
dialog titles say "GvrShell". It announces itself to the GVR event log as `HerculesInit( GvrShell )`.
So the OEM `NASCAR_Shell.reg` and `GvrShell.reg` belong to this binary. There is no separate,
missing GVR shell executable. Everything the cabinet ran is present.

The strings below locate the launch code. Their addresses are virtual addresses, so read the base
warning under the table.

| String | VA | Meaning |
|---|---|---|
| `In GvrGameLaunch` | `0x63DBCE40` | trace string inside the launch routine |
| `ERROR: %s trying to launch %s in %s` | `0x63DC0D5D` | its failure path: exe, folder, args |
| `Launch` | `0x63DC4F80` | the method name exposed to scene scripts |
| `.\PlugIns\%s.dll`, `GvrPlugInManager` | n/a | plug-in loading |
| `onGvrLoadComplete`, `onGvrControlUpdate`, `onGvrCustomBegin/End` | n/a | scene script event hooks |

**Its image base is `0x63B00000`, not `0x400000`.** This one detail can silently waste an
afternoon. Unlike with `NASCAR_GVR.exe`, Ghidra and x32dbg addresses only agree when the base
matches. Confirm it with `module_get_main` / `module_list` before trusting any address here.

### 9.2 The scenes are packed, which is why the registry names appear in no binary

Every `.am` file shares the magic `C3 0B 0A 00`. A search of the whole install for `launchName` or
`launchFolder` finds them only in the OEM reference `.reg`. The reason: the literals live inside
the compressed scenes, which pass them to the plug-in at runtime.

Scenes present: `Shell.am` at 648 KB, `NASCAR_Attract.am` at 5.1 MB,
`NASCAR_Selection.am` at 6.8 MB, `Launch_Game.am`, `Launch_Game_Error.am`, `DongleError.am`,
`WindowsRestart.am`. `Launch_Game.am` and `DongleError.am` exist as separate scenes, and that is
what makes the flow below readable.

### 9.3 The launch chain

```
Shell.am  ->  NASCAR_Attract.am  ->  NASCAR_Selection.am  ->  Launch_Game.am
   -> Operator_GetRegistry("launchName" / "launchFolder" / "cmd")   [NASCARPlugIn.dll, MANAGED]
   -> Launch(...)  ->  GvrGameLaunch  ->  CreateProcess             [AMPlayer.exe, native]
   -> NASCAR_GVR.exe -track <track> -startPos 43 ...
```

`Operator_GetRegistry` / `Operator_SetRegistry` read and write any named value under
`HKLM\SOFTWARE\GlobalVR\NASCAR`. The scene supplies the value name. So the registry plumbing is
real even though no binary contains the names.

**Trap for the next person:** `NASCARPlugIn.dll` does contain a `CreateProcessA` call. It sits
inside `_runRegistrationProgram` and launches the db_propagate / registration tool, not the game.
Do not mistake it for the handoff to the game.

### 9.4 Managed vs native — use the right decompiler

Ghidra produces convincing nonsense on the managed binaries. Both managed plug-ins decompile
cleanly with the project's ILSpy setup: `Tools\Decompile-Managed.ps1`, or `ilspycmd` from
`re-agent\Tools\ilspy`.

| Binary | Kind | Notes |
|---|---|---|
| `AMPlayer.exe` | native x86 | GvrShell. Holds the launch code. |
| `NASCARPlugIn.dll` | MANAGED | 680 KB. The operator menu. |
| `GvrPlusDEPlugin.dll` | MANAGED | Plus accounting |
| `GvrScriptedInputPlugin.dll` | MANAGED | |
| `GvrDiagnosticsPlugIn.dll` | MANAGED | |
| `PLUSDE.dll` | MANAGED | Mixed-mode, see section 5c. |
| `DongleStorageDevice.dll` | MANAGED | mixed-mode, HASP statically linked |
| `ZeusIOPlugIn.dll`, `HerculesPlugIn.dll`, `GvrMusicPlugIn.dll`, `GvrDirtyWordPlugIn.dll`, `GvrLinkingPlugIn.dll`, `GvrMotionPlugIn.dll`, `GvrIO.dll`, `Dongle.dll` | native | |

Decompiled here: `NASCAR\Decompiled\NASCARPlugIn\` with 40,507 lines, and
`NASCAR\Decompiled\DongleStorageDevice\` with 636 lines.

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

The plug-in is full of `GvrPlusConstant.*_PGA4` types, such as `CourseStats_PGA4`,
`HoleResult_PGA4` and `GamePurchaseStats_PGA`. This confirms that the shell derives from the PGA
golf codebase. It also explains the dead `C:\NASCAR_TOUR_GOLF_2006\...` paths in the operator-menu
XML.

### 9.6 The shell-side dongle gate — a DIFFERENT check from the game's

This is the current blocker, and it is **not** the gate solved in section 8.3. That gate is inside
`NASCAR_GVR.exe`, and the 39-byte stub in `GvrIOShim` satisfies it. The stub verifies the host
module's bytes, so it deliberately leaves `AMPlayer.exe` alone.

The shell's route:

```
Operator_GetDongleCabinetId                              [NASCARPlugIn.dll]
  -> new GvrSmartDevice(engine, gameName, GVRSDType = 2)  [2 = DONGLE]
  -> GvrSmartDevice.ReadHeader
  -> GVRStorageDevice front end
  -> DongleStorageDevice.dll                              [MANAGED, HASP statically linked]
  -> hasp(service, seed, lptnum, pass1, pass2, &p1,&p2,&p3,&p4)
```

HASP services used: 6 for presence / id, 50 = 0x32 for memory read, and 51 = 0x33 for memory
write. The decisive logic is tiny:

```c
IsPresent() : for each of two password pairs: hasp(6,300,0,pass1,pass2,...)
              return true when (p3 == 0 && p1 != 0)
GetType()   : return state[+16] ? 2 : 0      // 2 = DONGLE, only once HASP was found
GetSize()   : 112                            // the HASP memory image, in bytes
Read()      : returns 5000 unless state[+16]; else hasp(50,...) in 16-bit words
```

Everything cascades from that one presence call. With no hardware, `IsPresent` is false and
`GetType` returns 0. The device is then not a dongle, so the shell shows `DongleError.am` and
exits.

`GVRSDType` constants (from section D / the ABI report): 0 = PLAYER, 1 = OPERATOR, 2 = DONGLE,
3 = HOURLY.

### 9.7 Why the obvious file swap does not work

`DongleStorageDevice.dll`, `GVRSDEmulator.dll` and `GVRSCR28.dll` all export exactly the same two
functions: `CreateGVRStorageDeviceImp` and `ReleaseGVRStorageDeviceImp`. All three sit behind the
same `GVRStorageDevice` front end, so they are interchangeable back ends. That makes a drop-in
replacement the natural fix.

But copying `GVRSDEmulator.dll` over `DongleStorageDevice.dll` will **not** work.
`GVRSDEmulator`'s `GetType()` returns 0 (PLAYER), so the shell would see a blank player card
instead of a dongle. It has two more documented defects. `GetId()` never writes its out-param, and
its RAM image is zeroed every run, so nothing persists. See `GVRSCR28_ABI_REPORT.md` §D for its
full per-slot behaviour. It remains the best *reference implementation* of the vtable.

A correct replacement therefore needs:

* `GetType()` returning 2.
* `IsPresent()` returning true.
* `GetSize()` returning 112.
* A stable `GetId()`.
* File-backed `Read`/`Write` over a 112-byte image. Its contents must satisfy
  `GvrSmartDevice.ReadHeader`, which is schema-driven (see the ABI report §B.3).

Deploy it as `DongleStorageDevice.dll` and keep the OEM as `DongleStorageDevice_oem.dll`, per the
usual drop-in convention.

### 9.8 End-to-end test result (2026-10-03)

The test launched the real path: `NascarLaunch.exe`, which starts `AMPlayer.exe "Shell.am" -v FULL`.
It monitored every child process for 240 s:

* The shell boots and is healthy: 118 MB, 29 threads, responding. This matches the known-good
  attract state.
* The GVR event log records `HerculesInit( GvrShell )`, then `HerculesInit( AMPlayer )`.
* The registry carries `launchFolder`, `launchName` and
  `cmd = -startPos 43 -windowed -width 1280 -height 720 -noabort -notimeout`.
* **`NASCAR_GVR.exe` never spawned, and no new `trace00N.txt` was written.** Process monitoring
  and the file system both confirmed this. The game writes a trace on every launch.
* The shell reported the dongle missing/invalid and exited.

So the whole chain works except the shell-side dongle back end. Nothing else is missing.

---

## 10. The shell's dongle self-test — fully traced (2026-10-03)

The game, `NASCAR_GVR.exe`, and the shell, `AMPlayer.exe`, check **two different dongles**. The
game checks an Aladdin HASP, which is statically linked. The 39-byte stub in GvrIOShim already
satisfies it (section 8.3). The shell checks a GVR smartcard read through `GVRSCR28.dll`, and that
is the current blocker. The two are unrelated. A fix for one does nothing for the other.

### 10.1 Where the on-screen message comes from

`Shell.am` holds a startup self-test screen, stored in UTF-16 at offset 0x19CC. Its fields are
`DongleGame`, `DongleVersion`, `DongleRegion` and `ErrorMessage = "DONGLE MISSING OR INVALID"`.
A sibling condition in the same window is `"There is a CD in the drive."`. This confirms a general
power-on self-test, not a one-off check. The DLL hits for "missing or invalid" are unrelated PC/SC
error-table boilerplate inside GVRSCR28/PCSCSCR2. The scene is the real source.

When the user sees the error, the three fields read `Game: n/a  Version: n/a  Region: n/a`. So all
three dongle accessors returned empty.

### 10.2 The call chain, and the single upstream gate

The scene calls three methods exposed by `GvrPlusDEPlugin.dll` (managed):

```
IsValidDongle()      -> GvrSmartDevice(engine,"NASCAR",GVRSDType=2); DongleInserted(0);
                        ReadHeader(); GetMemObject("Header.GameName");
                        strstr(first 8 bytes,"NASCAR") ? true : false
GetDongleID(out)     -> ...; DongleInserted(0); GetId(); format as string
GetDongleData(k,out) -> ...; DongleInserted(0); read field k
```

All three gate FIRST on **`GvrSmartDevice.DongleInserted()`**, in `PLUSDE.dll`. A measurement
showed this. A software `GVRSCR28.dll` device was deployed that logs every call. After
`DongleInserted`, the device was interrogated zero times: no IsPresent, GetType, GetId, Connect,
GetStatus or Read. So `DongleInserted` returns false before the storage device is ever touched.
That is why every device-level fix was downstream of the real decision.

### 10.3 Everything that was eliminated, and why

Each attempt ruled out one hypothesis:

| Attempt | Result | What it proved |
|---|---|---|
| swap stock `GVRSDEmulator` in as `GVRSCR28.dll` | still rejected | `IsPresent=true` alone is not enough |
| patch emulator `GetType` to return 2 (DONGLE), live | still rejected | presence plus type is not enough |
| provision `GvrDongle`/`DongleInfo`/`GvrCabinet`/`CabinetBilling` rows | still rejected | not a DB-row lookup |
| set GlobalVariable `CabinetId=1` | still rejected | not that variable |
| SQL trace (GVRSQLITE_LOG) | 32 GlobalVariable reads, no dongle query at all | the check never hits SQL |
| own software device (good GetId, GetType=2, file-backed) | 0 interrogation calls | the gate is upstream of the device |
| patch `GvrPlusDEPlugin.IsValidDongle` to return true (dnlib NativeWrite) | plug-in loads, still rejected | the self-test uses the field accessors, not just IsValidDongle |

### 10.4 Why the data is genuinely absent

`GetMemObject("Header.GameName")` needs the field `Header.GameName`. On a dongle-less cabinet,
that field is defined **nowhere**. The only file in the whole install containing that string is
`GvrPlusDEPlugin.dll` itself, as the literal it passes. The definition lives on the real dongle's
own header. So the fields the self-test prints (`GameName`, `Version`, `Region`) come off the
dongle. Without one, they are simply not present.

### 10.5 Corroboration from an external source

A collector's HASP dump list gives NASCAR's dongle contents as `NASCAR1.5CTRYXX_CAB0`, with
`1.0`/`2.0` variants too. This matches exactly:

* `IsValidDongle` does `strstr(Header.GameName[0..7], "NASCAR")`. The first 8 bytes, `NASCAR1.`,
  contain `NASCAR`. PASS.
* `1.5` matches this cabinet's `RUNTIMEOEMVERSION = 1.5.0.04`.
* `CTRYXX` is the region field (`XX` = region-free), and `CAB0` is the cabinet type.

So a genuine dongle passes because `Header.GameName`/`Region`/`Version` decode to real values. A
content-reconstruction fix must reproduce that data.

### 10.6 Artifacts produced

* `NASCAR\src\GvrDongleEmu\`: a software `GVRSCR28.dll` back end. It reports type 2, has a working
  GetId and a file-backed image, and logs every call via GVRDONGLE_LOG. It is correct, but the gate
  never reaches it.
* `NASCAR\Tools\Patch-GvrDongleCheck.ps1`: a dnlib/NativeWrite patcher for managed boolean gates.
  It is idempotent and keeps `*_oem.dll`. It patched `IsValidDongle` cleanly, but that alone is not
  enough.
* `Tools\Provision-GvrDongle.py`: a schema-driven dongle-row provisioner. It is reusable, but not
  the fix here.
* `GvrSqlite.cs` log path fixed: it honours `GVRSQLITE_LOG` as a path and defaults to `%TEMP%`. It
  used to be a hardcoded `C:\gvrsqlite.log`, which failed silently without admin. That was a real
  bug.

### 10.7 State at the end of this attempt

Two routes remained:
1. Continue patching the `PLUSDE` chain from `DongleInserted` upward. Risk: PLUSDE is the shared DB
   core, and the number of layers is unknown.
2. **Content reconstruction**, the preferred route. Build a `Header.*` image that the UNPATCHED
   `DongleInserted`/`ReadHeader` accept, served by the software `GVRSCR28.dll`. The self-test then
   passes on data instead of on removed checks. Real dongle contents satisfy every check at once.
   This route needs the header layout/schema, which the ABI report flags as not yet recovered.

The install at that point had a patched `GvrPlusDEPlugin.dll`, with the OEM at `_oem`. It also had
a software `GVRSCR28.dll` + `PCSCSCR2.dll`, with the OEMs at `_oem`. None of these was
load-bearing, and all are reversible.

> **Superseded by section 11.** Section 10's conclusion that the gate sits "upstream of the
> device" was wrong. The shell's self-test never uses the PLUSDE path at all.

---

## 11. The shell runs end to end (2026-10-03, night)

Starting from "DONGLE MISSING OR INVALID", the shell now passes every power-on check. It loads
`NASCAR_Attract.am` ("NASCAR Start Your Engines"), `NASCAR_Selection.am` and the operator menu,
confirmed in play. The subsections below list each blocker in order, with its fix.

### 11.1 How to see what the shell is doing (read this first)

* The scene scripts are readable. Every `.am` embeds its JavaScript behaviours as zlib streams
  (`78 9C` / `78 DA`). `Tools\Extract-AmScripts.py <file.am>` inflates them all, so grep works on
  them. In `Shell.am` the startup self-test is `Behavior95` (stream at `0x28873`). The dongle slide
  logic is `Behavior92` at `0x4dad3`, and cabinet linking is `CabinetComm` at `0x8a130`. The scripts
  name every slide, every check and every `Gvr.Call`. Reading them is far faster than binary
  archaeology.
* Slides of `Shell.am`: `Checks` ("Please Wait..."), `Steering`, `PedalCalibrate`,
  `DongleError`, `CDError`, `NotSynced`, `CardReaderError`, `CardDispenserError`, `IOBoardError`,
  `MotionError`, `IOBoardVersionError`, `CabinetNumberSelect`.
* The startup state machine is `startUpChecks` in `Behavior95.onUpdate`. Its states run in this
  order:
  * 1 to 3: dongle game, version and region.
  * 4: shell init. It runs `NASCAR_INITIALIZE_SHELL`, uptime stats, service credits, restart
    bookkeeping, `GammaSet.exe`, `CabinetComm.startComm()` and `NASCAR_CD_IN_DRIVE`.
  * 7 to 9: IO board checks, after a deliberate 15 s `ioBoardDelay`. They test `CoinError != 3`,
    `IOBoardMajorVersion >= 3` and `IOBoardMinorVersion >= 3`.
  * 11: resolution (`rundll32 nvcpl.dll,dtcfg setmode`) and the pedal-range check.
  * 12: steering, only after an unscheduled Windows restart.
  * 13: bad buttons and cabinet-number collision.
  * 14: motion.
  * 15: `Gvr.LoadPresentation(path + "//NASCAR_Attract.am")`.
* **A script exception is silent.** If any `Gvr.Call` throws, the rest of that frame's `onUpdate`
  is skipped. The *same state then re-runs next frame, forever*. The screen sits on whatever slide
  was showing, or stays black. Nothing is logged by default.
* Anark's logger is a developer-only COM server that never shipped. `AMPlayer.exe` calls
  `CoCreateInstance({DE411D6F-2BDA-444C-AC87-9171BAFD9E99}, IID {18725949-89C4-460C-A092-75520BFFB542})`,
  then prints `FAILED to create AKLog2 interface.`
  * `Tools\AKLogShim\` is a stand-in, registered per user without admin. It forwards the player log
    to `OutputDebugString`. It also arms a vectored first-chance exception logger, which records
    module+offset, the C++ type name and a short stack.
  * Slots AMPlayer uses: `+0x1C (a,b)`, `+0x24 ()`, `+0x28 (record*)`. The record is 0x42C bytes,
    with the UTF-16 message at `+0x8` and the source file at `+0x21C`.
  * The shim refuses `AKPlu*.dll` callers, since they may use slots whose signatures are unknown.
  * It is what found the per-frame exception in 11.4.
  * To register it, run
    `SysWOW64\reg.exe add "HKCU\Software\Classes\CLSID\{DE411D6F-2BDA-444C-AC87-9171BAFD9E99}\InprocServer32" /ve /d <path>\AKLogShim.dll`
    plus `/v ThreadingModel /d Both`. Delete that key to remove it.
* `Tools\dbwin.py <out> <seconds>` captures `OutputDebugString` from every process.
  `Tools\stacksample.py <pid> [n] [tid]` samples a WOW64 thread's EIP and stack return addresses.
  The scene runs on the CController thread, not the window's main thread, so sample that one. Its
  id is logged as `Running Thread CController 0x...`.
* `DebugMode` (`HKCU\Software\Anark\Client\3.0\Preferences`, DWORD) exists but shows nothing on its
  own. The right-click popup menu has no console.
* Unelevated, `AMPlayer.exe` is registry-virtualised. Its HKLM writes land in
  `HKCU\Software\Classes\VirtualStore\MACHINE\SOFTWARE\WOW6432Node\GlobalVR\NASCAR`, and its reads
  prefer that copy. Look there for the script's `RegWrite` side effects: `Result_Status`,
  `UpTimeStart`, `MaxLinkId`, `FirstTimeStartup`, `FullScreenWidth/Height`.

### 11.2 The dongle: the reader is `Dongle.dll`, not PLUSDE

`Behavior95` calls `Gvr.Call("CheckDongle")`. The call goes through `ZeusIOPlugIn` and GvrIO to
`Shell\bin\Dongle.dll`, a GvrIO plug-in with the PDB `c:\Dev\Zeus\Shell\GvrIO\Release\Dongle.pdb`.
The script then tests three values:

* `Gvr.DongleGame == "NASCAR"`.
* `Gvr.DongleVersion == "1.0"`, not 1.5.
* `Gvr.DongleRegion` ∈ {`US`, `XX`}.

The PLUSDE accessors (`GetDongleData` / `GetDongleID`) appear only in `OPERATOR_MENUS\XML\*`.
PLUSDE picks the storage back end by type: `GVRSCR28.dll` for types 0/1/3, and
`DongleStorageDevice.dll` for type 2. That, and not an upstream gate, is why the `GVRSCR28`
software device saw zero calls in section 10.

`Dongle.dll` embeds the same HASP record library as `NASCAR_GVR.exe` (section 8.3).
`0x10004660(idx)` zeroes a 27-byte record at `0x10006068 + idx*0x1B` and calls the filler
`0x10004410`. The filler reads the 112-byte key memory `M` with HASP services 1, 5, then 0x32 for
0x38 words. It slices `M` into the record like this:

* `rec[1..6]=M[0..5]`: game.
* `rec[8..A]=M[6..8]`: version.
* `rec[C..D]=M[13..14]`: region.
* `rec[10..13]=M[9..12]`, `rec[15..17]=M[16..18]`, `rec[19]=M[15]`.

Region codes the game accepts: `US CA UK EU AU IE IT NZ ZA XX`.

Plug-in message IDs:

* `0x2`/`0x23`: presence. It fires callback `0x24`.
* `0x25`: present flag.
* `0x26`: game. `0x27`: version. `0x28`: region. `0x29`: cab.
* `0x4E`: set index.
* `0x12345678`: re-read.

sMessage layout: `+4` id, `+8`/`+0xC` args, `+0x14` handled.

**Fix:** `Tools\Patch-DongleDll.py <Shell\bin>` replaces only the filler. The replacement is a
28-byte position-independent stub that copies a record built from the image `NASCAR1.0CTRYXX_CAB0`.
Relocations inside the stub are neutralised. The OEM is kept as `Dongle_oem.dll`, and `--restore`
undoes the patch. With `1.5` (the collector's dump) the shell says "Invalid Dongle: Incorrect Game
Version".

### 11.3 "Please Wait..." forever — `NASCARPlugIn.dll` never loaded

`NASCARPlugIn.dll` imports `MSVCP71D.dll`, the debug C++ runtime, which needs `MSVCR71D.dll`. Both
ship in the disc's `WINDOWS\system32` file group but were never staged. So the plug-in failed to
load silently, with no `Loaded .\PlugIns\NASCARPlugIn.dll` line. Every `NASCAR_*` call failed, and
case 4 never advanced. **Fix:** stage both app-local in `Shell\bin`.

### 11.4 Black screen — `DateTime.Parse("")` every frame

Case 4 reads `UpTimeStart` / `UpTimeEnd` and calls `NASCAR_STATS_UPDATE_UP_TIME`. Its
`Stats_Update_UpTime` does `DateTime.Parse(startTimeStr)`. On a fresh install both values are
empty, so it throws a `FormatException` every frame. The writes that would fix this come *after*
the call. A real cabinet seeds them through the `FirstTimeStartup = 1` first-boot branch.

**Fix:** seed both (`yyyy-MM-dd HH:mm:ss`) in `HKLM\SOFTWARE\(WOW6432Node\)GlobalVR\NASCAR`. For an
unelevated test, seed them in the VirtualStore copy instead. After one good pass the shell
maintains them itself.

### 11.5 Host-hostile behaviour inside `NASCARPlugIn.dll` (the game-only rule)

* It changes the PC's time zone. Its settings init applies the cabinet zone
  (`"Turkey Standard Time"`, UTC+3) with `SetTimeZoneInformation`. It did so on the dev machine,
  and on 2026-07-20. Fix: rename the import to `GetTimeZoneInformation` at file offset `0x8D442`,
  where `S` becomes `G`. The name keeps its length and has a compatible signature. The assembly is
  not strong-named.
* `NASCAR_CD_IN_DRIVE` = `CreateFileA("D:\")` (native, `0x1000CF50`). On the cabinet, `D:` was the
  CD drive. So any PC with a `D:` drive gets "There is a CD in the drive". Fix: change the string
  `"D:\"` to `"?:\"` (file offset `0x38380`, single reference).
* It imports `ChangeDisplaySettingsExA`. Case 11 runs
  `rundll32 nvcpl.dll,dtcfg setmode 1 <w> <h> 32 60`, and frame 1 also runs `setmode 1 800 600`.
  Modern NVIDIA drivers reject `dtcfg`: result 8, desktop unchanged on an RTX 3060 laptop. Old
  drivers would obey. Case 11 also writes `FullScreenWidth/Height` (1360×768 here).
  **Fixed 2026-10-05 (installer 4.2, release 1.0.2):** a PC with no 32-bit `nvcpl.dll` showed
  Windows' "RunDLL: There was a problem starting nvcpl.dll" box on every start. A PC that has one
  never shows it. Both `Gvr.Launch("rundll32 nvcpl.dll,…")` statements in `Shell.am` `Behavior95`
  are now `;`. They sit in record `0x28867`, script lines 36 and 308. The change was made with
  `Tools\Edit-AmScript.py` with the compressed size kept, and the patch table carries it.
  `Tools\Test-BytePatches.py` checks that the table reproduces a working install exactly.
* Case 4 launches `Shell\GammaSet.exe -gammasetting 3 -digitalvibrance 15`. A 32-bit `NvCpl.dll`
  existed on the dev host, so it can really act. Fix: rename it to `GammaSet.exe.arcade-disabled`.
  The installer's `Disable-GammaSet` does this.

Both one-byte patches are applied to `Shell\bin\plugins\NASCARPlugIn.dll`. The OEM is kept as
`NASCARPlugIn_oem.dll`.

### 11.6 IO board and pedal calibration — a `GvrIOMessageSend` overlay

The shell's `GvrIO.dll` is byte-identical to the game's OEM copy, so `src\GvrIOShim` applies
unchanged. Its dongle patch verifies the host's bytes and leaves `AMPlayer` alone. The shim now
also overrides `GvrIOMessageSend`. Every other export still forwards to `GvrIO_oem.dll`.

| Message (ZeusIOPlugIn getter) | No hardware returns | Shim answers |
|---|---|---|
| `0x2F` IO board major (`0x100023A0`) | 0 | 3 |
| `0x30` IO board minor (`0x100023E0`) | 0 | 3, so firmware 3.03 |
| `0x39` axis min. `+8` = axis (0 X steering, 1 Y gas, 2 Z brake) | 255 | 0 |
| `0x3A` axis max | 1 | 255 |

The script needs major ≥ 3, minor ≥ 3, and `AxisMax − AxisMin ≥ 40` for gas and brake. Otherwise
it shows `IOBoardError` / `IOBoardVersionError` / `PedalCalibrate`. The shim substitutes an answer
**only when the OEM's is missing or below the threshold** (for an axis: OEM max < 40). So a real
board or wheel always wins. `GVRIOSHIM_NO_IOBOARD=1` disables the overlay.

`ZeusIOPlugIn`'s property table is `{getter, setter, name, type, 7}`, but a naive read is off by
one entry. Disassemble the getters instead. `CoinError` belongs to `HerculesPlugIn` and was not 3.
The rebuilt shim is deployed to `Shell\bin` only. The game's `Game\GvrIO.dll` is the previous
build.

### 11.7 ~4 fps in the shell — `HerculesPlugIn` waiting on a coin monitor that is not there

Measured, not guessed: the shell used ~19 % of one core while showing ~4 fps on either GPU. Stack
samples put half of the main thread's time in `WaitForSingleObject` inside `HerculesPlugIn.dll`.
The helper is `0x100048E0`. It is reached from `0x10006665` and from `0x1000219B` =
`connection->Receive(1000)`, with `push 0x3E8`.

Hercules talks to `GVRCoinMonitor.exe` over a shared-memory `MemoryConnection` (`%s_map` /
`%s_event` / `%s_mutex`). Every poll waited out its timeout because the monitor is not running
("ERROR: GvrCoinMonitor is NOT Running."). The plug-in is a debug build (`0xCC` stack fill,
`_RTC_CheckEsp`).

Not the fix: running `Hercules\GVRCoinMonitor.exe` from the disc. It is a cabinet daemon that
writes `C:\Hercules\CoinCount.bin` and can inject keystrokes (`keybd_event`).

**Fix:** the single wait helper `0x100048E0` takes its timeout from `[ebp+0xC]`. At file offset
`0x4904`, patch `8B 45 0C` to `33 C0 90`. In assembly, that turns `mov eax,[ebp+0xC]` into
`xor eax,eax; nop`. Every
Hercules wait then becomes a zero-timeout poll. All ten callers are the connection's mutex/event
waits, and an uncontended mutex is still acquired instantly. The OEM is kept as
`HerculesPlugIn_oem.dll`. CPU went from 19 % to 41 % of a core, now rendering instead of waiting.

Also measured on this hybrid laptop: `AMPlayer` renders with OpenGL and defaulted to the Intel iGPU
(`ig9icd32.dll`). A value under `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` moves it to
the NVIDIA GPU (`nvoglv32.dll`): `"<path>\AMPlayer.exe" = "GpuPreference=2;"`. The same works for
`NASCAR_GVR.exe`. That alone did not fix the frame rate. The Hercules patch did.

The same wait has a second copy in the operator menu. After the Hercules patch some operator menus
were still slow. `NASCARPlugIn.dll` statically contains its own copy of the Hercules client.
`MemoryConnection::Receive(timeout)` at `0x1002D0A0` does
`WaitForSingleObject(receiveEvent, timeout)`. The operator code calls it with 1000 ms
(`push 0x3E8` at `0x1002C1C4`) whenever a menu asks the coin monitor for meters/coins.

Fix: at file offset `0x2D0AC`, change `8B 4C 24 08` to `33 C9 90 90`. In assembly, that turns
`mov ecx,[esp+8]` into `xor ecx,ecx`. The receive then returns an instant "no message". Only `Receive` is touched, not
the mutex waits. This is the third byte patch in `NASCARPlugIn.dll`.

### 11.8 Still open

* ~~`NASCAR_Selection.am` loads `c:\NASCAR\Shell\...` by absolute cabinet path~~: fixed, see
  section 12.
* Starting a race from the shell works. Confirmed: a full race from 23:38 to 23:43 ended in
  `~ fini ~` and returned to the shell. It was simply reached via the car-select timeout.
* Sections 11.2 to 11.6 are applied by `Install-NASCAR-GVR-Portable.ps1`.

---

## 12. Operator menus and car select (2026-10-04)

End state, confirmed in play: operator Info loads ("Region: United States", IO Board 3.03), and car
select shows the drivers. Three separate defects stacked on the car-select screen. Each stayed
invisible until the one before it was fixed.

### 12.1 The trap behind most of it: a missing `Gvr` method or property aborts silently

Anark **silently abandons the whole call chain** when a script uses a `Gvr` method or property that
no loaded plug-in provides. This applies to scene scripts and to operator-menu XML `<LOGIC>`. There
is no exception, no "Script Error" record and nothing in any log. The screen just stops where it is.

Two plug-ins are deliberately not loaded on a desktop install, so everything that reaches for them
is a trap:

* `GvrLinkingPlugIn`: cabinet-to-cabinet linking. It hid the window at 100 % CPU.
* `GvrMotionPlugIn`.

How to find these calls:

* `Tools\Find-AmMissingCalls.py <Shell>` lists every `Gvr.Call("<method>")` in every `.am` that no
  loaded plug-in provides, nor `AMPlayer.exe` itself. Some remain after the fixes below. All of them
  are on screens a desktop install does not use, and none is patched:
  * `PGA_*`: golf leftovers on the Propagate / Modem Test screens.
  * `ACCOUNTING_IS_PLUSPOINTS`, `NASCAR_GET_PHANTOM_CREDITS`, `NASCAR_TOURNAMENT_GET_LOCAL`
    (smart-card purchase screens).
  * `MotionMotor`: Motion Test.
* For the operator XML, scan `<GETPROPERTY>` / `<CALL METHOD=>` names the same way.

### 12.2 Operator → Machine Settings → Info stuck on "Loading. Please Wait..."

The dialog appears when an item is activated, and only "table loaded...." clears it. The Info
table's MAC-address cell is `<GETPROPERTY>MacAddress</GETPROPERTY>`, a `GvrLinkingPlugIn` property.

**Fix:** that cell becomes `<STATIC_TEXT><VALUE>N/A</VALUE></STATIC_TEXT>` in
`OPERATOR_MENUS\XML\MachineInfoCol.xml`. Likewise, the four
`<CALL METHOD="NASCAR_GET_CONTACT_HEADER">` cells in `SysStatusCol.xml` become static blank, since
nothing provides that method. The OEM is kept as `*.xml.oem`.

Not yet fixed, untested: `MachineSettingsCol.xml` uses `<GETPROPERTY>MotionForce</GETPROPERTY>`
(`GvrMotionPlugIn`) as a dropdown value.

Operator-menu keys, from the screen legend: View `V` = up, Music `M` = down, Start `S` = select,
Look Back `L` = back.

### 12.3 Car select ("Start Your Engines", then a 24 s timeout auto-picks a car)

`NASCAR_Selection.am` has these slides, in order: `SelectDriver`, `WipeEnd`, `SelectTransmission`,
`SelectTrack`, `Lobby`, `LoadGame`. "Start Your Engines" with an empty white bar **is**
`SelectDriver`. The bar is where the driver-name image slides in, and a 3D car slides in over the
background. The `Timeout` behaviour picks a car after 24 s. That is why only the transmission ever
seemed selectable. `DriverSelect_Behavior` steers with the wheel, `Gvr.AxisX`. It selects with the gas
pedal (`Gvr.AxisY`) or Start.

1. Absolute cabinet paths. The scenes held 24 car-skin / driver-name literals
   `"c:\\NASCAR\\Shell\\Textures\\..."`. They also held `Gvr.MusicDir = "c:\\NASCAR\\Shell\\Audio"`
   and two slide backgrounds. Fixed with `Tools\Patch-AmPaths.py --map "c:/NASCAR/Shell/Textures/=Textures/"
   --map "c:/NASCAR/Shell/Audio=../Audio"`. Anark resolves `remoteSource` relative paths against
   the .am's folder (`Shell\`). The native music plug-in resolves `MusicDir` against the working
   directory (`Shell\bin`). An absolute install path did not fit (see 12.5). Menu music came back
   with this fix.
2. The cabinet was "international". `NASCAR_IS_INTERNATIONAL` (native `0x10009780`) is
   `strcmp(countryCode, "XX") == 0`. `countryCode` defaults to `"US"`. It is overwritten only when
   `Operator_Init` reads a PLUSDE dongle header successfully.
   * The leftover software `DongleStorageDevice.dll` (GvrDongleEmu, section 10) answered with a
     blank image. The region lookup then fell through to `"XX"`, and the operator Info page said
     "Non-US".
   * `onInitialize`'s international branch then raised script errors every frame. That branch runs
     `Driver_Group.detach(KaseyKahne / JeffGordon / JimmieJohnson /
   KyleBusch)`.
   * Fix: restore the OEM `DongleStorageDevice.dll`. The software one is parked as
     `DongleStorageDevice.dll.gvrdongleemu`. With no HASP key, the OEM header read fails and the
     block is skipped. The country stays `"US"`, so all 12 drivers appear.
3. The real blocker: `CabinetComm.sendPacket()` calls `Gvr.Call("SendPacket")`.
   * `resetDrivers()` calls `updateDriver()`, which calls `scene.CabinetComm.sendPacket()`. That
     ends in a `GvrLinkingPlugIn`-only call, which aborted the chain every frame (12.1).
   * As a result `firstFrame` never cleared and `frameCounter` never reached 5. So
     `onPlaySelectAnim`, which slides the car and name in, never ran. Meanwhile `resetDrivers`
     re-assigned the car skin 60 times a second.
   * **Fix:** `Tools\Patch-AmCalls.py --method SendPacket <Shell>` replaces that statement with `;`.
     It does so in all 17 scenes that carry `CabinetComm`: Shell, Attract, Selection and 14
     operator screens. The OEMs are kept as `*.am.oem`. Skin loads on entering car select dropped
     from thousands to 7.
4. Steering on a PC. `Gvr.AxisX/Y/Z` = GvrIO message 5 on the analog device (`+0` = 1, `+8` =
   axis). The shell's `GvrIO.dll` shim now answers it when no analog device exists.
   * The shim decides this from the gas axis max (< 40). The OEM's uncalibrated steering max is
     ≥ 40, so its garbage value would otherwise pass through.
   * The shim answers with centred values. While a window of the process has focus, `←`/`D` = −60
     and `→`/`G` = +60 for steering.
   * A real wheel wins.

How it was found: inflate the scripts with `Extract-AmScripts.py`. Then use `Tools\Edit-AmScript.py`
to insert checkpoint `output()` lines into `DriverSelect_Behavior`. The tool recompresses at exact
size, and `--strip-comments` frees room. The checkpoints printed "before updateDriver" every frame
and "after updateDriver" never.

AKLogShim's script-error decoding located the international-branch errors. The record carries a
va_list at `+0x210` with the behaviour name and line. Script line *N* is the *N*-th line after the
line holding `<![CDATA[`.

Ruled out: the renderer. Anark's right-click Rendering Engine → DirectX looked identical, and the
`DisableGL` preference does not switch it.

### 12.4 Reading the car-select screen

`NASCAR_Selection.am` behaviours:

* `DriverSelect_Behavior` (stream `0x23dd0d`).
* `Selection_Behavior`: the per-driver selector timeline. It fires `onPlaySelectAnim` after
  `bigHeadPause` = 12 frames at the centre key.
* `TrackSelect_Behavior`, `TransmissionSelect_Behavior`, `Timeout`.
* `Header_Behavior`: slide exits and `Stats_*Time` registry writes.
* `CabinetComm`: linking.
* `Globals`: `DoMotionResize` shrinks layers on motion cabinets only.

Track list on the disc: Daytona, Talladega, Lowes, Indianapolis, Phoenix, Bristol. The selector
table names ~30 tracks, but only six are enabled.

### 12.5 The `.am` container, for anyone editing scenes

The container uses tag-length-value records. A script record has this layout: tag `0x1770`, LE32
length = uncompressed size + 22, 4 zero bytes, `ANRK`, 4 zero bytes, LE32 uncompressed size, then a
zlib stream. There is **no compressed-size field**. Enclosing records, however, store physical
lengths: the root's length = file size − its header.

So a rewritten record must keep exactly its original compressed byte count. `Patch-AmPaths.py`'s
`deflate_exact` does that. It tries exact-size full compression first. If that fails, it uses a
normal head plus a raw stored tail sized to fit. If that fails too, it uses a sync-flush split.
Only the record's two uncompressed-size fields change.

A replacement *longer* than the original usually cannot fit. Prefer shorter text, such as relative
paths or `;` instead of a call, or strip comments. In replacement strings use a function, not a
string. `re.sub` would treat the doubled JS backslashes as escapes, and that bug produced
`..Textures`.

### 12.6 Still open

* ~~Track select is skipped, and every race is Daytona~~: **RESOLVED 2026-10-04.** Confirmed:
  tracks are selectable, and a race was logged on a second track in `TrackStats_NAS1`.
  * `TrackSelect_Behavior` (frame 5) calls `trackSelectedByOther()` when
    `tracksEnabledCount <= 1`. That call auto-picks a track and shows no screen.
  * `Globals` builds that count from `Gvr.Call("NASCAR_DISABLE_TRACKS_GET_VALUE", i)`, i = 0..5
    (Daytona, Talladega, Lowe's, Indianapolis, Phoenix, Bristol).
  * That method (native `0x1000D590`) returns `(cabConfigInt(0x1E) & bitTable[i]) > 0`. Despite
    the name, a set bit means the track is AVAILABLE.
  * Config int 30 is `CabinetConfiguration_NAS1.TracksDisabled`. The OEM row ships it as 0, which
    means no tracks.
  * Bit tables: country ≠ "XX" uses `0x10096848` = {1, 2, 4, 0x10, 0x20, 0xFFFFFFFF}. "XX" uses
    `0x10096830` = {1, 2, 4, 8, 0x10, 0x20}.
  * 63 (0x3F) enables all six. It can be set through Operator → Game Settings → Disable Tracks.
    `Build-NascarSqliteDb.py` now provisions 63.
  * A first attempt that set the column directly was wrongly recorded as "no effect". That test
    run went into the operator menu and never reached track select, and the value was then
    reverted.
  * With 63 the Disable Tracks screen builds normally: six names, six `GET_VALUE` = 1.
* The Disable Tracks screen "froze" once while the value was 0: the menu beeped and the screen
  stopped. It has not been reproduced since, and the screen has worked normally after that. Its
  table (`TracksDisabledCol.xml`) uses only `NASCARPlugIn` methods.
* "Game Crashed" after a race means `Result_Status` 0. `Launch_Game.am` maps 0 to "Game Crashed",
  1 to finished and 2 to timed out. The game writes 0 for any unfinished race.
  * In `trace014.txt` the race reached `INGAME` at 7.15 s. Then it logged `#### ESCAPEREQUESTED ####`
    and a clean `~ fini ~`. That is an abort, not a crash.
  * The source of the escape is unconfirmed, possibly an Esc/Q key press.
  * The shell's own command line is
    `-track DAYTONA -car 20 -mode SINGLE -racelength 0 -nofe -series 2005NEXTEL -handling 0`. The
    registry `cmd` is appended to it.
* Keyboard steering in car select is wired: axis 0 is now queried and answered. It scrolls
  drivers/transmissions, as the `onSelectionChanged` events in the capture show.
* Still open: the `MotionForce` dropdown (12.2) and the unused-screen calls listed in 12.1.
* The fixes in sections 11 and 12 are applied by `Install-NASCAR-GVR-Portable.ps1`.

---

## 13. Display: resolution limit, borderless, always-on-top, taskbar (2026-10-04, evening)

Confirmed in play: the race is sharp at 1920×1080 and borderless. Alt-tab and the taskbar behave.

### 13.1 The engine drops every display mode larger than 1600

The race's video manager constructor (`0x507C46`) sets the accepted mode range. The minimum is
640×480. The maximum dimension `[mgr+0x1F64]` is 1600 (`0x507C5A: C7 82 64 1F 00 00 40 06 00 00`).
It also sets `+0x1F68` to 1200, which is unused here. A special case cuts the maximum to 1024 for
PCI vendor `0x104A`.

The D3D9 enumeration loop (`0x50841D`) calls `GetAdapterModeCount`, `EnumAdapterModes` and
`CheckDeviceType`, with bpp ≥ 16. It skips any mode that is wider or taller than that maximum. The
maximum is read only at `0x5084BC`.

The modes form a linked list off the adapter object `[0x78E374]`. The adapter holds the count at
`+0x120` and the list head at `+0x124`. Each node holds the width at `+8`, the height at `+0xC`, the
format at `+0x14`, the refresh rate at `+0x18` and the next node at `+0x20`.

So on a 1920×1080 monitor the list stopped at 1600×900. It held 30 entries: 15 sizes × 60/144 Hz.
That looked like a 30-entry cap, but it is not. `-width 1920 -height 1080` was rejected for both
fullscreen and windowed (`*ERROR* ... passed on the command line is invalid!`). The race then fell
back to `** Using default screen resolution: 800 x 600`, stretched over the screen.

**Fix:** `GvrIOShim` `patch_mode_limit()` rewrites that immediate value to 4096 in memory before
`main()`. The bytes are verified. `GVRIOSHIM_NO_MODE_PATCH=1` skips the patch. `NASCAR_GVR.exe`
stays unchanged on disk.

Result: `Using windowed screen resolution 1920 x 1080 from the command line` and
`Selected VideoMode: 32`.

The shim's shell-only overrides now apply only to `AMPlayer.exe`. They cover the IO board, the
pedals and keyboard steering. In the race, every GvrIO message passes through untouched.

### 13.2 `nascar_settings.ini` defaults explained the "small window"

The launcher appends `[Display]`'s `-windowed|-fullscreen -width W -height H` to every race the
shell starts (registry `cmd`). The shipped default is 1280×720 windowed. So the race ran in a small
window while the shell was full screen. Set Width/Height to the desktop size.

### 13.3 Launcher: borderless, always-on-top, taskbar (`src\NascarLaunch`)

The launcher runs one 250 ms loop while it waits on the shell (`WaitForExit=true`). The loop does
three things:
* `[Display] Borderless=true`: the race runs `-windowed`. The loop strips the race window's caption
  and frame and places it over its monitor. This overrides `Fullscreen`. Width/Height 0 means the
  screen size.
* The shell window is `WS_EX_TOPMOST`, as the log shows on every start. That swallowed alt-tab. The
  loop demotes it with `SetWindowPos(HWND_NOTOPMOST, …, SWP_NOACTIVATE)`. It demotes the race window
  too, unless the race runs in exclusive fullscreen. There Direct3D owns the flag.
* The shell hides the taskbar. The loop shows it again whenever neither `AMPlayer.exe` nor
  `NASCAR_GVR.exe` is the foreground window.

### 13.4 The 15 s "Please Wait..." and its loading bar

Nothing loads during this wait. Startup step 4 sets `ioBoardDelay = presentation.realTime + 15.0`.
Step 8 waits for it before it reads the IO-board version. This gives a real Nytric board time to
boot.

The bar is cosmetic: `scaleFactor *= frameCounter / 750` while the `Checks` slide is up. The attract
presentation is loaded only after the checks.

On the cabinet the shell autostarts at Windows boot. The wait gave the USB board time to enumerate
before the shell read its firmware version. Reading too early returns 0, which gives a false "GVRIO
board" error.

**Done 2026-10-04:** `Tools\Edit-AmScript.py` changed `Shell.am` `Behavior95` from
`ioBoardDelay = presentation.realTime + 15.0;` to `+ 0.5;`. The edit is at record `0x28867` and
keeps the compressed size. The previous file is `Shell.am.pre-iobdelay`, and the pristine one is
`Shell.am.oem`. The 0.5 s re-check in step 8 is unchanged.

Confirmed: startup is visibly faster.

The change is safe on a real cabinet too. The board-presence check runs in step 7, before this
delay. When `IOBoardMajorVersion == 0` it is a hard stop with no retry. So the 15 s never protected
against a late board. The delayed step-8 version check re-checks every 0.5 s on its own. For a
cabinet that autostarts at boot, delay the launcher instead.

---

## 14. Gamepad support — and how the race really takes the cabinet's input (2026-10-04, night)

Goal: Xbox and PlayStation pads in the front end and the race. Everything below was read out of
`NASCAR_GVR.exe` with Ghidra and out of `GvrIO_oem.dll` with capstone. The pad code is
`src\GvrIOShim\GvrIOPad.cpp`.

### 14.1 The race drives from GvrIO, not from its `.CTL` controller lines

* `NASCAR_GVR.exe` calls `DirectInput8Create` exactly once, at `0x45bc33` in `HWInput::Setup`
  (`Source\Pc\Input\hwinput.cpp`). It creates only the keyboard (`0x457410`: `GUID_SysKeyboard`,
  `c_dfDIKeyboard`, device type `0x13`). `c_dfDIJoystick` (`0x6f2684`) is linked but referenced
  nowhere. So this build never reads the `.CTL` "controller 1" bindings, such as `Accelerate (1,4)`.
  This corrects §5b.
* `HWInput::Setup` then calls `cGvrIO::Initialize(5 /*Steering*/, hwnd, callback 0x456f80)`. Every
  frame, `HWInput::Update` (`0x45be20`) calls `FUN_0045c650`. That function first sends GvrIO msg
  `(1, 1)`, the pump. Inside that one call, GvrIO polls its devices and its own keyboard and
  dispatches events through the callback. Then the function sends msg 5 for axes 0/1/2, and msg
  `0x42` with the names `Shifter1`..`Shifter4`.
* Axis values are `v / 255` (`0x744dc0`). Steering is doubled and signed, so about −127..127 is full
  lock. Mode `[cfg+0x62b8]` 0 squares it. Gas and brake are 0..255. The values land at
  `0x91a654/58/5c`, with "nonzero" flags at `0x91a660/61/62`.
* The vehicle-input function sits at `0x45a420`. It is not a Ghidra function: it starts right after
  the `ret 4` at `0x45a41d`. It takes those GvrIO values whenever they are nonzero. Otherwise it
  ramps the digital cabinet buttons: Forward = gas, Back = brake, Left/Right = steering (`GvrIO.xml`
  keys `r c d g`). With manual transmission (`0x91a860`), the gear `shifty` follows the shifter. It
  is logged as `vehicleinput::shifty=%d`. Shifter4 gives 4, 3 gives 3, 2 gives 2, 1 gives 1, and no
  shifter position gives 3.

### 14.2 The race's GvrIO buttons (callback `0x456f80`, msg id `0xB`, event name at `+0xC`)

| Event (from the `GvrIO.xml` name) | State byte | What the race does |
|---|---|---|
| `onGvrForward/Back/Left/RightButtonDown/Up` | `0x91a663..66` | digital gas / brake / steer (14.1) |
| `onGvrViewButtonDown/Up` | `0x91a667` | the rising edge calls `FUN_00419130("Drivable")`: next driving camera |
| `onGvrLookBack(2)ButtonDown/Up` | `0x91a668` | rear view while held (`[esi+0x3c] = 1`) |
| `onGvrStartButtonDown/Up` | `0x91a669` | `GVRRestartRunning` (`0x485f40`): puts the car back on the track at the nearest waypoint. It has a cool-down and a restart counter. |
| `onGvrNOSButtonDown/Up` | `0x91a66a` | (Re)starts vehicle sound slot 61 every frame while held (`FUN_004f7710(…, 0x3d, …)`, `eax = 1`). It never stops it. Tried as the horn on 2026-10-04: one press and it sounded forever. NASCAR's `GvrIO.xml` has no `NOS` entry, so on the cabinet this is dead code. Leave it. The working horn is the engine's `.CTL` `Control - Horn` (14.4). Superseded on 2026-10-05: the final horn uses this NOS input (14.6). |
| `onGvrMusicButtonDown/Up` | `0x91a66f` | next song (`0x4a83c0`) |
| `onGvrMotionDisableButtonDown/Up` | `0x91a670` | motion-seat toggle, irrelevant off-cabinet |
| msg `0x42` `Shifter1..4` | `0x91a66b..6e` | the 4-position H-shifter (manual only) |

**Four-button abort:** `FUN_004829b0` ends the race when View + LookBack + Start + Music are held
together. It logs "`Player initiated ABORT!`" and sets `DAT_0078e6e0 = 1`. This works in single
player only. It also needs `HKLM\Software\GlobalVR\NASCAR\FourButtonAbort` to be nonzero
(`FUN_004818c0`). The installer and the OEM `NASCAR.reg` set it to `1`. Operator → Game Settings can
change it.

The race's `-noabort` switch does not disable the abort. The option table at `0x762e34` has 49
`{name, id}` entries. It is parsed at `0x45d3f0` into flag bytes at `0x91c710+id`. It maps `noabort`
to id `0x22`, which is the flag byte `0x91c732`. That flag only stops the checkpoint timer from
ending the race (`local_4 = 999.9` in the same function).

### 14.3 GvrIO's keyboard is the cleanest place to inject buttons

* `cGvrIO::Initialize` (`0x10002650`) is linear, with no early exit when no board is present. It
  loads `GvrIO.xml` from `HKLM\SOFTWARE\Gvr\Plus\2.0\Cabinet\GameRoot` + `Config\`. It creates the
  board objects, then the keyboard. The keyboard code (`0x10001c50`) makes its own
  `DirectInput8Create` with `IID_IDirectInput8A`, then `CreateDevice(GUID_SysKeyboard)` with
  `c_dfDIKeyboard`. Finally the function loads plug-ins, registers the callback and starts
  its worker.
* The pump polls the keyboard with `GetDeviceState(256)` (`0x10001b60`). It turns each pressed scan
  code into a letter with `MapVirtualKeyA(sc, MAPVK_VSC_TO_VK)`, so it follows the keyboard layout. A
  held `<key char>` then behaves exactly like the board button of the same `name`. GvrIO sends
  `onGvr<Name>ButtonDown/Up` (id `0xB`, built at `0x100053ab`) and the XML `event`. It also adds the
  name to a held-names list (`0x1000ca18`, `0x10005140` / `0x100051a0`). That list answers msg
  `0x42`, which asks "is `<name>` held" (case at `0x1000529a`).
* So a pad needs no event plumbing. **OR its buttons into the 256-byte state of GvrIO's own keyboard
  object**, and GvrIO raises every event itself, in both programs.
* GvrIO's own selection moves one step per press of `VK_LEFT` / `VK_RIGHT`. It is `Gvr.SelectionX` =
  msg `(2, 0xF)`, with the answer via the pointer at `+0x10`. `SelectionRangeX` is msg
  `(1, 0x13, n)`, and the event is `onSelectionChanged`. The code at `0x10005591` clamps to
  `[0, range-1]`. It then calls callbacks `0x15`, `5` and `0xB "onSelectionChanged"`. Transmission
  select uses this selection instead of `Gvr.AxisX`, so a pad must press those keys.
  Through `MapVirtualKeyA`, the real arrow keys' DirectInput codes `0xCB`/`0xCD` map to no virtual
  key. `0x4B`/`0x4D`, the numpad 4/6 position, give `VK_LEFT`/`VK_RIGHT`. So on a keyboard, numpad
  4/6 changes the transmission choice.
* The race reads its own DirectInput keyboard the same way (`GetDeviceState`, `0x457630`). That
  keyboard carries the `.CTL` keyboard bindings and the numpad Swingman (chase) camera. Engine
  controls with no cabinet button can be reached only there.

### 14.4 Implementation (`GvrIO.dll` shim, deployed to `Game\` and `Shell\bin\`)

* `GvrIOPad.cpp` is new and wired from `GvrIOShim.cpp`. The pad reader handles XInput in any of the
  4 user slots. It also handles DS4 raw HID over USB or Bluetooth, through the gamepad collection.
  Feature `0x02` gives the full Bluetooth report. Pad discovery runs on a background thread, so the
  game threads never pay for an empty XInput slot or a HID scan. Polling an empty XInput slot
  re-enumerates devices on every call, a 100%-CPU trap. Unplugging and replugging is picked up.
* Buttons. `cGvrIO::Initialize` is already overridden. It first redirects `GvrIO_oem.dll`'s
  `DINPUT8!DirectInput8Create` import, to learn which `IDirectInput8A` belongs to GvrIO. Then it
  redirects two entries of DirectInput's shared vtables: `CreateDevice` (slot 3) and
  `GetDeviceState` (slot 9). The hooks act only for GvrIO's objects. Every other object passes
  straight through. After a successful `GetDeviceState` on GvrIO's keyboard, the mapped cabinet
  letters get `0x80`. The hook uses `MapVirtualKeyA(VK, MAPVK_VK_TO_VSC)`, so AZERTY works. A failed
  read is left alone. That happens when the window is not in front.
  Trap in the first build: giving the objects private vtable copies broke everything. That build
  swapped each object's vtable pointer. `dinput8` validates `this` against its own vtables, so
  `CreateDevice` failed. GvrIO then had no keyboard at all, and even the physical cabinet keys died.
  Reading the live shell's memory found it. The object at `GvrIO_oem+0xC160` had both its keyboard
  pointer `+0x4C` and its mouse pointer `+0x50` at 0. Meanwhile the `IDirectInput8` carried the
  copied vtable.
* Menu selection. In the front end, D-pad left/right also press `VK_LEFT`/`VK_RIGHT`. So does the
  left stick held over half-way. Transmission select then works like the cabinet wheel: stick right
  = Manual.
* Axes. Msg 5 (device 1) is answered from the pad when the OEM reports no usable gas range. The test
  is `real_pedals()`: msg `0x3A` axis 1, max < 40. This now applies in the race too. Before, the race
  was pass-through. A real cabinet wheel always wins. Out of focus, the pad reads neutral. In the
  menus only, D-pad left/right steer ±60 like the keyboard.
* Gears. Shifting is sequential over the 4-position shifter. Square/Triangle step through gears
  1..4, and the shim holds `t y u i` for the current gear. This works in the race process only and
  is ignored in automatic. Every race starts in 1st.
* Quit. Hold D-pad down for 1 s. The shim then holds `v l s m` together, which triggers the
  four-button abort.
* Race-only keys. The horn part of this bullet is superseded by the 2026-10-05 horn note in 14.6.
  `inject_race_keys` uses the same `GetDeviceState` hook on the race's own keyboard, with raw scan
  codes.
  * The horn holds J. `Game\Save\GvrSinglePlayer\GvrSinglePlayer.CTL` now binds
    `Control - Horn="(0, 36)"`. The OEM value `(0, 89)` means unbound, and `.CTL.oem` is kept. So the
    keyboard's J is the horn too.
  * Not H: the engine reads H itself as its "cycle HUD mode" hotkey. The modes are normal, hidden
    and an "Internal Pkts" debug overlay (`0x91ac20+0x23` at `0x4785d7`). The engine also hard-codes
    F G M P S T X Z Home End F8 F11 F12 and the numpad.
  * The right stick, held over half-way, holds numpad 4/6 to turn and numpad 8/2 to move up and
    down. This drives the Swingman camera.
  * The engine's horn starts on press and stops on release. The first build's `GvrIO.xml` `NOS`
    line was removed again, because `GvrIO.xml` is the OEM file.
* Map. `nascar_settings.ini` `[Controller]` holds the race map, and `[Frontend]` the menu map. Both
  use `action = button` lines with named buttons. The defaults are in `map_defaults()`.
  `GVRIOSHIM_NO_PAD=1` disables all of it. The previous shim is kept as `GvrIO.dll.pre-pad`. The
  exports are unchanged (11).

### 14.5 Pad layout

| Pad | Race | Menus |
|---|---|---|
| Left stick | steering | car / track select (wheel) |
| R2 / RT | gas | gas pedal = select |
| L2 / LT | brake | — |
| Cross / A | — (no e-brake on this cabinet) | select (Start) |
| Circle / B | horn (the engine's Horn control, J) | back (Look Back) |
| Right stick | chase-camera turn (numpad 4/6/8/2) | — |
| Square / X | shift up (manual, 4 gears) | — |
| Triangle / Y | shift down | — |
| L1 / LB | look back | — |
| R1 / RB | camera (View) | — |
| Options / Menu | Start = put the car back on track | operator |
| D-pad up | — | up (View) |
| D-pad down | **quit: hold 1 s** (four-button abort) | down (Music) |
| D-pad left / right | — / music | left / right (+ steer the selectors) |
| R3 | — | — (no card reader) |

The horn row describes the first build. Since 2026-10-05 the pad's horn holds J as the cabinet NOS
input (14.6).

### 14.6 Status

Confirmed 2026-10-04 with a PS4 DualShock 4 over Bluetooth (raw HID, report `0x11`, 547 bytes).
Working: menus, driver, transmission and track select with Manual included, steering, gas and
brake, the buttons, manual shifting, the horn and the right-stick camera. Not separately verified:
hold-to-quit. Not tried: an Xbox pad, which uses the XInput path.

`nascar_settings.ini` `[Debug] Log=true` writes `gvrioshim.log` next to each executable. The setting
is read when each program starts. The environment variable `GVRIOSHIM_LOG=1` does the same. The log
records hook installation, pad found/lost, the map in use, and every change of the held cabinet and
race keys.

**Horn, final (2026-10-05, confirmed):** the engine's `.CTL` `Control - Horn` makes no sound in this
build. It was tested on H and J, with keyboard and pad. The only horn is the cabinet NOS input:
* `GvrIO.xml` gets `<key char="j" name="NOS" …>`. J is used because H is the engine's HUD-cycle
  hotkey.
* The pad's horn holds that key.
* `GvrIOShim.cpp` `patch_horn_stop()` reroutes `0x4DA348` through a stub. The stub stops vehicle
  sound 61 when the NOS flag drops. The sample loops, and the race never stopped it. The flag was
  watched live: it does return to 0.

The stop recipe is copied from the race's checkpoint-warning code at `0x482BD5`:
1. The slot is owner+`0xA80`, or `+0x10CC` if byte owner+`0xC8` is 0. Add `0xE8` + id×`0x14`.
2. The handle pair goes into `ebx`/`esi`.
3. Call `0x4A73F0`.
4. Clear the active byte owner+(id×9+`0x34`)×4.

`GVRIOSHIM_NO_HORN_PATCH=1` skips it. `Control - Horn` is back to the OEM `(0, 89)`. This supersedes
the §14.4 "race-only keys: horn holds J" bullet.

---

## 15. A private registry - the game no longer uses the Windows registry (2026-10-05)

Goal: no start failures from a missing registry, no administrator rights, and no collision with
another GlobalVR title on the same PC. `HKLM\SOFTWARE\gvr\Plus\1.1\Cabinet` is really shared between
titles. NASCAR's `PlusSchemaPath` sits there next to other titles' values.

Who reads what, by the value names found in the binaries:
* the shell scripts: `launchFolder`, `attractFolder`, `selectFolder`, names
* `NASCARPlugIn.dll`: resolution, `FourButtonAbort`, `UpTime*`, `Prefix`/`Suffix`
* `NASCAR_GVR.exe`: resolution, `FourButtonAbort`, `StallMonitorTest`, `cmd`, `tracefilenum`
* `GvrParseXml.dll`/GvrIO: `GameRoot`
* the GvrPlus DLLs: `PlusSchemaPath`, `PublicKeyPath`, `PromotionPath`, `MaxObjectLoadCount`
* `GvrMusicPlugIn.dll`: `MusicVolume`

Only `Version`/`Build`/`CommVersion`/`wtf` are pure checks. So the values are answered, not deleted.

**Design:** the hook lives in `src\GvrIOShim\GvrIOReg.cpp`, in the GvrIO shim. It redirects the
advapi32 `Reg*` exports, which have a hot-patch prologue (`mov edi,edi` + 5×`CC`). Any key under
`HKLM\SOFTWARE\GlobalVR` or `HKLM\SOFTWARE\gvr` becomes a private handle with an odd value. This
covers either registry view, and keys opened directly or step by step. For step-by-step opens, the
parent paths come from `NtQueryKey`. The private handle is served from
`<install>\nascar_registry.ini`. That file has one section per key, values in quotes and DWORDs as
`dword:xxxxxxxx`.

Folder values are always computed from the install folder. Anything missing falls back to the OEM
defaults (`DEFAULTS`), and `UpTime*` falls back to "now". The first run copies the real registry's
values in, never overwriting. Every other key goes to the real registry.
`GVRIOSHIM_NO_PRIVATE_REGISTRY=1` turns it off.

Load order: the race imports the shim, so its DllMain runs before `main`. The front end reads
`GameRoot` before it loads plug-ins. So `NascarLaunch` starts `AMPlayer.exe` suspended and queues
`LoadLibraryA(Shell\bin\GvrIO.dll)` as an APC on its main thread. The APC runs during loader init.
The launcher now writes its ini-driven values (`FullScreen*`, `MusicVolume`, `SimpleAttract`, `cmd`)
into `nascar_registry.ini`. This needs no elevation, and the old HKLM self-repair is gone.

Verified in a scratch test: A and W calls, size queries, set/delete, step-by-step opens, and
unrelated keys left untouched. Verified in a real session on 2026-10-05: menus and a full race. The
store got the shell's `UpTime*`/`CommandLine` and the race's `tracefilenum=34`. The real registry
stayed at 33.

Two things were seen that the private registry does not cause. `Result_*` stay 0, as in the real
registry before. `AMPlayer` exits with 0xC0000005 after a session, as in four earlier sessions the
same day.

Installer: `Install-NASCAR-GVR-Portable.ps1` no longer writes these HKLM keys. It only creates
`nascar_registry.ini` (`FullScreenWidth`/`Height`) if it is missing. The hook engine lives in a
shared library, `src\GvrPrivReg`. `GvrIOReg.cpp` holds only NASCAR's roots and defaults.


---

## 16. A fresh install: the operator's first-time setup (2026-10-05)

Found by installing from the release folder onto a new folder and comparing it with the working
install.

* Symptom 1: "Calibrate the accelerator and brake" on the first start. It disappeared on the second
  start.
* Symptom 2: free play was off, and several operator options were on.
* **Cause: `NASCARPlugIn`'s `Operator_FirstTime_Init` ran.** It runs when the database
  `GlobalVariable` `gOperatorFirstTimeInitilized` is `0`. The misspelling is the game's own. The OEM
  data ships that value. The routine then does all of the following:
  * It writes `FirstTimeStartup = 1` to the registry. `Shell.am` shows `PedalCalibrate` whenever
    `FirstTimeStartup == 1`, whatever the pedal ranges are. It then writes `0`. That is why the
    screen appeared only once.
  * It runs `Operator_ResetFactoryDefaults`. That overwrites `CabinetConfiguration_NAS1` with
    `FreePlay 0`, the options at indexes 13-14, 19-20, 22-23 and 25-27 set to `1`,
    `TracksDisabled 65535` and `AttractVolume 90`.
  * It resets the pricing model and calls `Operator_SetTimeZone`.
* Fix:
  * `Build-NascarSqliteDb.py` provisions `gOperatorFirstTimeInitilized = 1`, so the routine never
    runs. That matches the working install, where the flag was already 1.
  * The private registry gains a `FirstTimeStartup = 0` default.
* The shim's pedal answer was correct all along. The shim log showed the OEM `GvrIO` answering
  nothing for axes 1 and 2 (range -1..-1). The shim reported 0..255. Its range test was still
  tightened to the script's own rule (max minus min < 40).

## 17. The front end's renderer and display device (2026-10-27)

Both front ends are Anark players. 1.1 uses Client 3.0, and 2.0.2 uses Client 4.0. Each has three
renderers: Direct3D 7 through D3DX7, OpenGL, and software (Pixomatic in 1.1).

* Choice. The value `ClientRenderer` under `HKCU\Software\Anark\Client\<3.0|4.0>\Preferences` wins
  when set. Otherwise the player picks Direct3D (3) if available. Next come OpenGL (2) and then
  software. `DisableGL`, `DisableDX7` and `DisableSW` (string `"1"`) each remove one from the list.
  1.1 runs with `DisableGL = "1"`, as on the cabinet, so it uses Direct3D 7. 2.0.2 uses OpenGL
  because its Direct3D path deadlocks on alt-tab (section 13 / MODS.md).
* OpenGL test (2.0.2 `sub_63B26C60`, `sub_63B44590`). The player opens a hidden window at 0,0. It
  hands the description of pixel format #1 to `ChoosePixelFormat` and calls `wglCreateContext`.
  Then all four of `GL_EXT_bgra GL_WIN_swap_hint GL_ARB_multitexture GL_EXT_texture_env_combine`
  must appear in `glGetString(GL_EXTENSIONS)`. Otherwise OpenGL counts as unavailable and is greyed
  out in the renderer menu. The renderer itself only logs missing features and null-checks
  `glAddSwapHintRectWIN`. Current Intel and NVIDIA drivers list all four. Windows' software OpenGL
  1.1 ("GDI Generic") lacks two. A window on a screen driven by a virtual display adapter gets that
  software OpenGL.
* Direct3D device list (1.1 `sub_63D1E912`, D3DX7 `CDDrawDeviceList`). It calls
  `DirectDrawEnumerateExA` with flags 7: attached secondary, detached secondary and non-display
  devices. Every device that opens is kept with its monitor. The render context creates DirectDraw
  with the chosen device's GUID (`DX7RenderContext.cpp`).
* Win32_VideoController (WMI: `AdapterRAM`, `DriverVersion`, `VideoProcessor`, current mode) is only
  collected into a system-information report. It does not choose the renderer.

What the package does about it:
* The launcher tests OpenGL like the 2.0.2 front end before every start. It then writes
  `ClientRenderer`: OpenGL for hardware OpenGL 1.3+, else Direct3D. The setting is `[Display]
  Renderer`.
* The shim adds the four old extension names for hardware OpenGL 1.3+.
* The shim offers only attached displays to the Direct3D device list. It logs every device and the
  one opened.

## 18. Team Racing's Windows event source "GVR" (2026-11-02)

The 2.0.2 Plus layer (`PLUSDE.dll`, managed `GvrDataEngine.LogEvent`) writes its events to the
`GvrEventLog` table only for a cabinet id above 0 on a "CABINET" tier. This package's free-play
setup uses cabinet id 0. With cabinet id 0, every event goes to the Windows Application log through
`System.Diagnostics.EventLog`, source `GVR`.

On .NET 1.1, `WriteEntry` first checks every event log for the source and creates it when missing.
As a normal user, the search stops with a `SecurityException` at the Security log. Creating a
source also needs administrator rights.

The exception surfaces in the front end's `NASCAR_INITIALIZE_SHELL`, and the menus stay black. The
script engine then faults (`AMPlayer.exe+0x28BE89` / `+0x28BEA2`, the JavaScript stack allocator).
The cabinet-state query repeats every frame (`gP2PMacAddressNumber`, `CabinetConfiguration_NAS3`,
`GvrCabinetSyncLog`).

Reproduced by removing every `GVR` source key. The fix registers the source at
`HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\GVR`, with `EventMessageFile` set to
.NET 1.1's `EventLogMessages.dll`. The installer does this. NASCAR Racing 1.1 does not take this
path.

## 19. .NET 1.1 inside the install folder (2026-11-11)

Both front ends are native `AMPlayer.exe` hosts. Their plug-ins, PLUSDE and the Plus layer, load CLR
1.1. Installing .NET 1.1 into Windows kept failing on players' PCs:
* A damaged install hung the front end with no window.
* The plain redist stops with MSI 1603 on Windows 10/11.
* The SP1 package (`NDP1.1sp1-KB867460-X86.exe`) is itself a .NET 1.x program. On a PC without any
  older .NET it cannot even unpack (`0x80131700`, failed to load the runtime).

Windows' own `mscoree.dll`, the .NET 4 shim, still honours `COMPLUS_InstallRoot` and
`COMPLUS_Version`. With `COMPLUS_InstallRoot=<install>\Runtime` and `COMPLUS_Version=v1.1.4322`, the
front end loads `<install>\Runtime\v1.1.4322\mscorwks.dll`. Nothing of .NET 1.1 has to be in Windows
then.

There is no global assembly cache in this setup. So the framework assemblies the front end binds
(`System`, `System.Data`, `System.Xml`, ...) must sit next to `AMPlayer.exe`. A missing one stops
the front end at a binding-error box.

Verified on a Windows 10 with no .NET 1.1: both front ends reach their menus. NASCAR Racing reached
attract and track select, and Team Racing reached driver select. The shim logs
`CLR loaded: ...\Runtime\v1.1.4322\mscorwks.dll`. The race (`NASCAR_GVR.exe`) is native and loads no
CLR.

The runtime is built at install time from the two Microsoft packages in `Dependencies\DotNet11`,
without running either installer:

1. `dotnetfx.exe /q /c /t:<dir>` unpacks `netfx.msi`. `msiexec /a` makes a file image of it and
   installs nothing. The runtime is the image's `Framework\URTInstallPath` folder (1.1.4322.573).
2. The SP1 package embeds three Microsoft cabinets (`MSCF` header, size at +8). The largest holds
   every updated file whole (74 files, 1.1.4322.2032). Each file is named after its Windows
   Installer file key, such as `FL_System_Data_dll_____X86.<GUID>` and
   `mscorwks_dll_4_____X86.<GUID>`. Keys are cut at 72 characters. `expand.exe` unpacks the
   cabinet. Each file replaces the runtime file whose name reads the same with "." as "_". A
   trailing `_<n>` is dropped, and a cut key is matched by its unique prefix.
3. `gdiplus.dll` is removed. A real install leaves it out, and Windows has its own.

Compared file by file with a real SP1 install, the result differs only in files the game does not
use. These are `gacutil.exe`, `netfxupdate.exe`, `Updates\hotfix.exe`, two ASP.NET SQL templates,
and the `CONFIG\security.config` / `enterprisesec.config` files that .NET writes on first use.

The launcher sets the two variables only when `<install>\Runtime\v1.1.4322\mscorwks.dll` exists. So
an install made before this change keeps using the .NET 1.1 in Windows. Team Racing's event source
`GVR` (section 18) takes its messages from the private `EventLogMessages.dll`.

## 20. A start-up deadlock: .NET inside Windows' DLL-loading lock (2026-11-14)

Both front ends load `GvrParseXml.dll` late, from a worker thread, through `LoadLibraryExA`. That
DLL is mixed C++/.NET and imports `mscoree.dll`. Its start-up runs managed code inside its
`DllMain`, so it runs while the thread holds Windows' DLL-loading lock.

On one PC (Windows 10 IoT Enterprise LTSC 2024, 24H2) the front end never showed a window. The
launcher's freeze capture showed:

- The loading thread, in call order: `AMPlayer`, `LoadLibraryExA`, `GvrParseXml`, `mscoree`, managed
  code, `mscorwks`, `CoGetObjectContext`, `CoUnmarshalInterface`, `rpcrt4`. The last one is an RPC
  call that waits.
- The I/O shim's thread waits in `LoadLibraryA`. Four new threads wait in `LdrInitializeThunk`. All
  of them are queued behind the lock.

CLR 1.1 asks COM for a thread's context the first time the thread runs managed code. The loading
thread is already a single-threaded apartment, through the game's own `CoInitialize`.
`CoGetApartmentType` logged this at the load, the same with and without the change below. So no
apartment is being created there.

The frozen call is COM remoting: `CoUnmarshalInterface`, then `CoSetProxyBlanket`, then RPC. The new
threads queued behind the lock are most likely COM's own helper threads. The same install starts
normally on other PCs (Windows 10 22H2, Windows 11 25H2), with or without spaces in the install
path.

A second PC (Windows 10 22H2, Portuguese) froze the same way. Its call path was
`CoGetObjectContext`, then COM's implicit `CoInitializeSecurity`, then RPC. It ran 1.2.1-beta.1,
which used the .NET 1.1 installed in Windows. It also froze with the install's own copy.

Change ("safe start"): as soon as the shim is loaded, threads of its
own do two steps.

1. They run one complete COM round trip. This uses a multithreaded apartment, kept with
   `CoIncrementMTAUsage`. A single-threaded helper calls `CoGetObjectContext` and marshals a minimal
   object with `MSHCTX_LOCAL`. The multithreaded apartment unmarshals it into a real proxy and makes
   one call through it.
2. They start .NET: `CorBindToRuntimeEx("v1.1.4322", "wks")` from Windows' `mscoree.dll`, which
   loads the install's private copy. Then come `ICorRuntimeHost::Start` and `GetDefaultDomain`.

The front end's `LoadLibraryA` / `LoadLibraryExA` calls wait for both steps, for at most 30 s. They
never wait when the caller already holds the loading lock. So `GvrParseXml.dll` finds COM set up and
the runtime running. On the test PCs both steps take 0-31 ms. `[Launcher] SafeStart=false` turns it
off.

The freeze capture now also logs the owner of the loading lock (PEB `LoaderLock`, owning thread) and
48 stack frames per thread.


## 21. Coin monitor (2026-11-23)

On a cabinet, `GVRCoinMonitor.exe` counts the coins and the menus ask it for the credit total. The
GvrIO shim can now do that job (`src\GvrIOShim\GvrIOCoin.cpp`). It runs only with
`[Coin] Enabled = true`, so Free Play installs behave as before.

### 21.1 How the menus talk to it

Each program has a mailbox made of three named objects: `<name>_map`, `<name>_event` and
`<name>_mutex`. The map holds the message size, a count, and up to 20 connected names of 20 bytes.
A client adds its name to the server's list and sets the server's event.

Each direction then has its own queue, named `<from>_to_<to>`. Its map is `10 * size + 128` bytes:

| Offset | Content |
|---|---|
| +0 | Message size. Both sides default to 64 bytes |
| +4 | Messages waiting, at most 10 |
| +8 + 12 * n | Message n: length, data offset, `GetTickCount` when sent |
| +128 + offset | Message data |

A message is an array of 32-bit values: unused, command, payload length, then the arguments. The
receiver drops messages older than 500 ms. A reply is command 2008 with a value, or 2009 with a
status code.

### 21.2 Commands

Credits have two slots, used as a bit mask: 1 = paid coins, 2 = service credits. A mask of 0 or
less, or of 255 or more, means both. Spending takes service credits first.

| Command | Arguments | Effect and reply |
|---|---|---|
| 2004 | amount | Spend from both slots. Status 1 if there is not enough |
| 2005 | | Empty both slots, reply with the old total |
| 2006 | pulses, meter | Add pulses to a mechanical meter |
| 2010 | | Total credits |
| 2021 | mask, amount | Spend from the masked slots |
| 2022 | mask | Empty the masked slots, reply with the old count |
| 2023 | mask | Credits in the masked slots |
| 2024 | mask, amount | Add credits. Service wins if both bits are set |
| 2032 | mask | Read and clear the spent counters |
| 2033 | mask | Read the spent counters |
| 2040 | on / off | Allow service credits. This command has no reply |
| 2041 | input | Coins counted on input 0 to 3. Status 8 for another number |
| 2042 | | Team Racing only: I/O board status |
| 2043 | input | Team Racing only: resettable count of input 0 to 4 |
| 2044 | input | Team Racing only: reset that count |
| 2045 | input | Team Racing only: lifetime count of input 0 to 4 |

The original keeps credits in `C:\Hercules\CoinCount.bin` and in HKLM. The shim keeps them in
`Game\Save\nascar_coins.ini`, which an update preserves.

### 21.3 The two wait patches

The installed `HerculesPlugIn.dll` (`0x4904`) and `NASCARPlugIn.dll` (1.1 `0x2D0AC`, 2.0.2
`0x3251C`) have their reply wait set to zero, because no monitor runs (section 11.7). With a zero
wait a client never sees a reply. In coin mode the shim restores the original bytes in memory. The
files on disk stay as installed.

The restored wait also covers `GVRVolumeMonitor`. The menus read `Gvr.MasterVolume` from it every
five frames, so an unanswered read would stall them for a second each time. The shim answers that
name too, always with 0, which is the value the menus get without a monitor.

### 21.4 The Collections page and dates

The Collections page finds unpaid-out payments with a filter such as
`CreationDate<='11/23/2026 6:26:50 PM'`. SQL Server compares dates by value, but SQLite compares
text. The database holds both `2026-11-23 18:08:41` and `11/23/2026 6:08:41 PM`, so the page found
no payments and showed 0.00. GvrSqlite now converts both sides of a date comparison to the first
form.

### 21.5 Team Racing's driver and track lists

These screens set `Gvr.SelectionRangeX = 5` and read the slot as a speed: 2 stops the list, 1 and 3
move it slowly, 0 and 4 move it fast. A cabinet wheel rests in slot 2 and springs back to it. With no
wheel the slot stayed at 0, so the list scrolled by itself and could not confirm a driver. The shim
now starts these screens in slot 2 and returns to it when the stick or key is released. It tells them
from other five-slot screens by `Gvr.SelectionFF = 0` (GvrIO message `0x1F`).

### 21.6 Team Racing's price plan

The 2.0.2 database has price plans 15003 and 15004. Its menus look up plan 15000, so the price lookup
fails. The on-screen price then falls back to 1 credit, while the charge falls back to 0.

With `[Coin] Enabled = true`, GvrSqlite adds plan 15000 once, when it first opens the database. The
plan copies 15004's prices, books to the existing categories 15000 and 15003, and sets 4 credits per
dollar. A copy of the database is kept as `game.db.before-coin`. Free Play installs are not changed.
