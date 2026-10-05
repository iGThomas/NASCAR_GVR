# NASCAR GVR — engineering log and plan

Append-only, newest entries at the bottom, same convention as the parent project's
`GAME_ONLY_INSTALL_PLAN.md`. Add dated findings here rather than scattering notes.

---

## 2026-08-12 — first pass: media identified, payload extracted, installer written

### Done

* Identified `RECOVERY DISC ISO EXTRACTED\GVR\NASCAR-20080826-1.GVR` as a **WIM image**
  (`MSWIM`, format 1.13) — the cabinet's XP-Embedded C: drive, 1.03 GB, 4,684 files.
  Extracted with 7-Zip to `Extracted\RecoveryImage`. It contains prerequisites as
  *installed state* (.NET 1.1, MSDE 2000 SP3 with system DBs only, SQLXML 3.0, DivX,
  `d3d9.dll`) plus real driver installers (NVIDIA 177.83/94.24, **HASP `hdd32.exe`**,
  Intel LAN, C-Media). It does **not** contain the game.
* Extracted the game disc's InstallShield 12 cabinets with the project's existing
  `release\Tools\unshield.exe` → `Extracted\Disc\File_Group`, 4,914 files / 800 MB.
  Five file groups: `NASCAR`, `Hercules`, `GVRPLUS`, `GVR`, `WINDOWS`.
* Mapped payload → cabinet layout from the OEM registry files and `GVRSETUP.INI`
  (see `NASCAR_FINDINGS.md` §2).
* Established the engine identity: **EA NASCAR** (`.MAS`, `.gdb`, `.sci`, Miles, Bink)
  under a GVR arcade shim, built from `\Projects\Nascar\Dev\Source\Pc\`.
* Recovered the full command-line arg table, track list (6 playable), series list, and the
  `GvrIO.xml` keyboard fallbacks.
* **`NASCAR_GVR.exe` has no SQL/.NET/PLUSDE dependency** — verified by import table. The
  database is a shell-only concern, so game-only needs no MSDE.
* Wrote `Install-NASCAR-GVR-Portable.ps1` (dry-run clean) and
  `Extract-NASCAR-Sources.ps1`, plus `README.md`, `NASCAR_FINDINGS.md`,
  `NASCAR_INSTALL_ANALYSIS.md`.

### Smoke test (host: Win11 x64, non-elevated, no registry keys written)

Ran the extracted `NASCAR_GVR.exe` in place with the GvrIO + MSVC 7.1 DLLs staged beside
it, windowed 800×600:

* survives >120 s, 7 threads, ~15 MB working set, main thread in `ExecutionDelay`
* creates `LOG\LOADTIME.txt` (0 bytes) → `main()` runs
* no window, no further output → **stalls in a probe/retry loop**

Matches the signature of the NFSU UniverShell2 idle hang (cabinet hardware probes), not a
crash.

### Known blocker

Dongle. The EXE carries `**  FATAL  ** NO DONGLE DETECTED!` and reads
`game/version/region/cab` off an Aladdin HASP key via `Shell\bin\Dongle.dll`. No
`-nodongle` switch exists. Attack point and ABI are in
`NASCAR_FINDINGS.md` §5.

### Next steps, in order

1. **Re-test after a real elevated install.** The smoke test ran without
   `HKLM\SOFTWARE\Gvr\Plus\2.0\Cabinet\GameRoot`, so GvrIO could not find
   `Config\GvrIO.xml` and may have been stuck on that rather than on the dongle. Run
   `Install-NASCAR-GVR-Portable.ps1` elevated, then `Play-NASCAR.cmd`, and capture how far
   it gets. This is cheap and could reclassify the whole blocker.
2. **Instrument the stall.** Procmon on the hung process will name the culprit
   immediately (HASP driver open? `\\.\` device? registry poll? XML file miss?). A Procmon
   log is how the parent project resolved equivalent hangs.
3. **Ghidra pass** on `NASCAR_GVR.exe` + `Dongle.dll`: recover `sMessage` and the dongle
   query/response message IDs. The parent project's Ghidra MCP setup and
   `Tools\Import-GvrTypes-Headless.py` workflow apply.
4. **Write `Dongle.dll` shim** exporting `Initialize`/`MessageCallback`/`Finalize`.
   Follow the `shell-oem-abi-forwarding` lesson: rename the original to `Dongle_oem.dll`,
   forward everything, override only the dongle answer. Same for `GvrIO.dll` if the I/O
   board probe also blocks (`GvrInputEmu`/`GvrShellPad` in the parent tree are working
   references for that pattern).
5. **Controller support — try config before code.** NASCAR keeps EA's data-driven controls
   stack (`Game\Save\GvrSinglePlayer\GvrSinglePlayer.CTL`, format decoded in
   `NASCAR_FINDINGS.md` §5b), so a pad/wheel is expected to be a rebinding job, not a
   `GVRInputRaw`-style DLL replacement. Only if the shim overrides the CTL does
   `Steering.dll` (same three-export GvrIO ABI) need replacing — and then the parent
   project's `controller-support-project` implementation is the reference to port.
6. ~~**Shell route.**~~ **Done 2026-08-12 — see the entry below.**

---

## 2026-08-12 (later the same day) — SQLite backend built and verified

Goal changed to the **full cabinet experience** (shell + accounting + leaderboards), not
just the race. That needs the GvrPlus database, so the NFSU SQLite route was ported.

### Done

* **Decrypted the OEM schema.** GlobalVR reused the GvrPlus AES-128-CBC key/IV across
  titles — the NFSU derivation works unchanged on NASCAR's 2008 `.enc` files. Reimplemented
  as `Tools\Decrypt-GvrEnc.ps1` using .NET's built-in AES (no pycryptodome needed).
  Yield: 106 `CREATE TABLE`, 716 stored procs, 366 seed rows.
* **`game.db` built and provisioned** (`Tools\Build-NascarSqliteDb.py`): schema + `_gvrmeta`
  type map + seeds + 32 `GlobalVariable` rows (which live in the schema file, not the
  content file) + unique index on `GlobalVariableId` (the OEM DDL has no PK there) +
  operator pricing from the 9 `a+*.tbl` files + 360 leaderboard/collection rows from the 7
  `Nascar*.tbl` files that `Nascar_db.exe` would bulk-load + synthesized free-play cabinet.
* **Provider extended and compiled for real .NET 1.1.** This box has
  `Framework\v1.1.4322\csc.exe`, so no dnlib retarget was needed (the fallback path is still
  in `Build-SqliteBackend.ps1`). NASCAR's PLUSDE needed four members NFSU's never called:
  `ExecuteScalar`, `CommandTimeout`, `SqlError.Message`, `SqlException.Message`. Also added
  proper `InsertDepth`/`UpdateDepth` handling (primary row written, nested propagation
  columns logged and skipped) instead of the old best-effort flat upsert.
* **`PLUSDE.dll` patched** (`Tools\Patch-PlusdeToSqlite.ps1`): 10 typerefs swapped, written
  with dnlib's native writer so the C++/CLI half survives (native imports unchanged,
  +4 KB metadata). Pre-flight refuses to patch unless the provider covers all 37 referenced
  members; post-write verify confirms 0 SqlClient typerefs remain.
* **Functional test passes 13/13** under the real 1.1 runtime against the built database
  (`Tools\Test-GvrSqlite.ps1`).
* **Installer integration**: the SQLite backend stages GvrPlus, drops the three DLLs
  beside both executables (keeping the OEM `PLUSDE.dll.oem`), installs `game.db`, and sets
  the env var. Existing databases are never clobbered without `-ForceOverwrite`.

### Trap found and fixed

`GVRSQLITE_DB` is machine-wide, and this box's NFSU install already points it at
`D:\Games\NFSU\Underground\GVR\GvrPlus\game.db`. A NASCAR install would have silently
opened NFSU's database (it did — `no such table: Tracks_NAS1`). The provider now checks a
per-title `GVRSQLITE_DB_NAS1` first, so both titles coexist on one machine.

### Still open

* The dongle stall in `NASCAR_GVR.exe` (unchanged — the database work does not touch it).
* `GvrPeEngine.dll` / `GvrPeClient.dll` still reference SqlClient (incl. `SqlDataReader`).
  They are the cabinet→server propagation engine and should be unreachable offline; if
  something does reach them it will throw a normal .NET error, not corrupt data.
* Everything above is verified at the database layer only. The shell has not yet been run
  against it end-to-end — that is gated on the dongle.

---

## 2026-08-12 (third pass) — cabinet registry mined from the recovery image hives

The recovery image carries the cabinet's **live registry**, which nothing on the game disc
exposes. `Tools\Dump-Hive.py` (new) reads offline `regf` hives directly — no `reg load`, no
admin, no mounting a 2008 XP-Embedded hive on a modern machine.

**The OEM-revision gate value, finally pinned down** (the NASCAR equivalent of NFSU's
`NFS - UG,XP Embedded,HW Rev 865 e,05052005`):

```
HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment
  RUNTIMEOEMREV     = NASCAR,XP Embedded,HW Rev 945-G31,08252008
  RUNTIMEOEMVERSION = 1.5.0.04
```

Both are required — `GVRSETUP.INI [MINIMUMOS]` checks product name, date **and** version.
Added `-SetOemRev` to the installer for anyone who wants to run the OEM `Setup.exe`; our
flow does not need it and the game binary never reads it.

Also recovered, and written up in `NASCAR_REGISTRY.md`:

* the rest of the XPe identity block (`ProductName`/`BuildDir` = NASCAR, `OSVERSION` 1.5,
  `RUNTIMESKUCODE` XPeCli, the XPe GUID/PID, `lib` → SQLXML bin)
* autologon as user `cabinet`, `Shell = Explorer.exe` (NASCAR hides Explorer rather than
  replacing the shell the way NFSU does)
* the `Run` entries the discs never ship: `netset.exe`, `HideTaskBar.exe`
* **the dongle driver stack**: services `akshasp`, `aksusb` (demand) and `Hardlock`,
  `Haspnt` (auto) — Aladdin HASP4 / HASP HL, with all five `.sys` files present in the
  image. That is exactly the API a `Dongle.dll` replacement must impersonate, which
  sharpens the §5 blocker considerably.

**No hidden game registry exists.** `SOFTWARE\GlobalVR`, `SOFTWARE\Gvr` and
`SOFTWARE\Tsunami` are all absent from the image — the image is the OS *before* the game
install, so the disc's `Registry\*.reg` files plus the game's own runtime writes really are
the complete set.

---

## 2026-08-12 (fourth pass) — reworked as a PORTABLE installer

The first installer reproduced the OEM cabinet layout — `C:\NASCAR`, `C:\GvrPlus`,
`C:\GVR`, `C:\hercules` — which is the wrong shape for a normal PC install and litters the
root of the system drive. Reworked to follow
`GIT\NFSU_GVR_Portable\Install-NFSU-GVR-Portable.ps1` instead, which is the mature pattern
this project already arrived at for NFSU.

`Install-NASCAR-GVR-GameOnly.ps1` is **replaced** by
**`Install-NASCAR-GVR-Portable.ps1`**:

* **Prompts for the install folder** (default `D:\Games\NASCAR`), refuses a drive root, and
  nests everything inside it: `Game\`, `Shell\`, `GVR\GvrPlus\` (with `game.db`),
  `GVR\Gvr_Tools\`, `Fonts\`, the two launchers.
* Every registry value is emitted **pointing at the chosen folder** — nothing assumes `C:`.
* `-DryRun` now works **unelevated**, so an install can be previewed without a UAC prompt.

Ported from the NFSU portable package (the accumulated "how it actually runs" knowledge):

| Ported | Note |
|---|---|
| `Ensure-DotNet` | .NET 1.1 **SP1 slipstream** — the plain redist dies with MSI 1603 on Win10/11, and CLR 2.0 redirection AVs inside PLUSDE's native C++ EH. Needed by the shell, not the game. |
| `Ensure-DirectX` | June-2010 redist, but **opt-in** here: NASCAR imports `d3d9` directly and no `d3dx9`, unlike NFSU. |
| `Deploy-Dxvk` | App-local DXVK + 60 fps cap, **opt-in**: NFSU needed it because its physics is framerate-tied; whether NASCAR's EA engine shares that is unverified, so it is not forced on. |
| `Deploy-Fonts` | Register **only families Windows lacks** (a `PrivateFontCollection` reads each family name), keep copies in `<ROOT>\Fonts`, `AddFontResource` + `WM_FONTCHANGE` so no reboot. Dropping the disc's stock Microsoft faces over the system ones breaks text everywhere. |
| Compile-on-target | The provider is rebuilt with the machine's own .NET 1.1 `csc` when present; the prebuilt `Deploy\GvrSqlite.dll` is the fallback. |
| Shared `Dependencies\` | Reused from the NFSU portable folder rather than duplicating 135 MB of redists. |

**New NASCAR-specific find while doing this:** the shell's operator-menu XML files carry
absolute paths — 4 files with `C:\NASCAR\Shell\Operator_Menus\...` and 5 with
`C:\NASCAR_TOUR_GOLF_2006\Shell\...` (dead leftovers from the PGA Golf codebase this shell
was derived from). This is the NASCAR analogue of the GVRD path repointing NFSU needed.
`Repoint-ShellPaths` rewrites all 9 to the chosen install folder; since the GOLF-path
targets (`Return_To_Game.am`, `Propagate.am`) do exist in our tree, that also fixes menu
entries which were broken on the real cabinet.

Not ported, and why: the `gvr_settings.ini` + `GvrLaunch.exe` mechanism (NASCAR takes
resolution/windowed/track/car as **native command-line arguments**, so a plain `.cmd` with
variables at the top does the same job with no launcher binary and no memory patching), and
the 4:3 restriction (NASCAR's cabinet ran 1360×768 — this engine does adapt to widescreen).

---

## 2026-08-12 (fifth pass) — real launcher, and the cabinet experience as the default

The portable rework still got the *experience* wrong: it generated `.cmd` files, and the
main one launched `NASCAR_GVR.exe` with a **hardcoded `-track DAYTONA`**. That is a
test harness, not a cabinet.

**The cabinet experience is the shell.** `AMPlayer.exe` runs `Shell.am` → attract mode →
the player presses Start → track/car selection → and the shell launches the race itself,
composing the command line from `HKLM\SOFTWARE\GlobalVR\NASCAR` (`launchFolder`,
`launchName`, `cmd`). Nothing about the track is ours to choose.

So, modelled on `GIT\src\GvrLaunch\GvrLaunch.cpp`:

* **`src\NascarLaunch\NascarLaunch.cpp`** → `NascarLaunch.exe` (155 KB, native Win32, no
  runtime dependency, game icon embedded, built by `build.cmd` via vswhere+vcvars).
* **`nascar_settings.ini`** — one commented file: `[Display]` resolution/fullscreen,
  `[Cabinet]` attract volume + simple-attract, `[Launcher] Mode=shell|race`, `[Race]` for
  the direct mode. Edit and just start the game; no tool to run.
* Desktop **and** Start-menu shortcuts point at the launcher.
* Both `.cmd` files are gone.

What the launcher does that a shortcut cannot:

1. Applies the ini to `HKLM\SOFTWARE\GlobalVR\NASCAR` in **both registry views**, including
   the `cmd` value — the argument string the **shell** passes to the race. That is how a
   resolution set in the ini reaches a game the launcher never starts itself.
2. **Compares before writing** and self-elevates (`runas`, `--apply-settings`) only when
   something actually changed, so a normal launch raises no UAC prompt.
3. Reads `GVRSQLITE_DB_NAS1` out of the registry and injects it into the child's
   environment — **this removes the log-off-and-back-on step** that the NFSU install needs,
   because the child no longer depends on Explorer having been restarted.
4. Hands the new window the foreground, and logs to `nascarlaunch.log`.

Unlike NFSU's launcher it patches **nothing in memory**: NASCAR takes resolution from the
registry and the command line, so there are no hardcoded constants to overwrite.

---

## 2026-08-13 — first real run: Procmon diagnosis, three bugs fixed

The install worked and the launcher worked, but `AMPlayer.exe` died ~4 s in with exit code
`0xC000041D` (STATUS_FATAL_USER_CALLBACK_EXCEPTION) and a WER `0xC0000005` at fault offset
`0x00170011` in module **"unknown"** — i.e. an access violation executing JIT'd memory, not
the dongle stall we expected. **The game never even got launched; the shell was dying
first.** Three separate defects, found in order:

### 1. Bracketed column types → wrong SQLite affinity (my bug)

`NASCARcabinet.txt` mixes two DDL styles. Most tables declare `[Col] bigint`, but
**`GlobalVariable`** declares `[GlobalVariableId] [bigint] NOT NULL`. `Build-NascarSqliteDb.py`
did not strip the brackets, so the type read as `[bigint]`, matched nothing, and the column
silently became `TEXT` — handing PLUSDE a `String` where it wanted `Int64`. Exactly one
table was affected and it is the first one the shell queries. Fixed with `normalize_type()`;
`_gvrmeta` now stores the normalized name too, so the provider's own type map sees `bigint`.

### 2. The filled DataTable had no name (provider bug, also affects NFSU)

Reproduced the crashing query in a 1.1 harness:

```
Fill returned 1 row(s)   table[0] name='Table1'
ds.Tables["GlobalVariable"] = NULL  <-- would throw in the caller
```

PLUSDE sets `DataAdapter.TableMappings` and then looks the result up **by name**. Our
`Fill` ignored the mapping and let ADO.NET default the name, so the caller got a null table
→ NullReferenceException inside mixed-mode C++ → AV. `GvrDataAdapter.Fill` now honours
`TableMappings`, falls back to the table in the `FROM` clause, and refills an existing table
instead of `Add()`ing a duplicate name.

### 3. THE BIG ONE: the shell was running on CLR 2.0

Process Monitor settled it. The shim probed for `AMPlayer.exe.config` → **NAME NOT FOUND**,
then loaded `v2.0.50727\mscorwks.dll` (52 hits) and `GAC_32\System.Data\2.0.0.0`. That is
precisely the failure the NFSU work documented: **PLUSDE's native C++ exception interop
access-violates on CLR 2.0; the stack needs REAL CLR 1.1.** `AMPlayer.exe` is a *native*
host, so unlike NFSU's managed EXEs it needs a config to *pin* the old runtime rather than
having a 2.0 binding removed.

Fix — `Install-ClrPin` now writes `Shell\bin\AMPlayer.exe.config` with
`<requiredRuntime version="v1.1.4322" safemode="true"/>`. After it, the trace shows
`CLR Security Config\v1.1.4322` and `GAC\System.Data\1.0.5000.0`, and the shell gets
**much** further: 16 successful `GlobalVariable` queries through cabinet initialisation
(`gCabinetIsRegistered`, `gAccountingRecordId`, `gOperatorFirstTimeInitilized`,
`gCurrCollectionStartTime`, … `RestartMode`) and it now logs its own events:

```
GVR event log:  HerculesInit( GvrShell )   HerculesInit( AMPlayer )
```

### Where it stands now

Still exits at ~3-4 s. The trace's last real activity is `GvrPlusDEPlugin.dll` + a `.pdb`
probe, so the fault is now inside **`GvrPlusDEPlugin.dll`** (the Anark plug-in that bridges
to PLUSDE), *after* the database initialisation completes. All nine plug-ins load fine from
`Shell\bin\plugins\`, System.Data 1.1 loads, every query returns rows.

Also worth recording, and ruled out as the cause:

* 8 of the seeded `GlobalVariable` rows have empty values (`gGameTitle`, `gGameVersion`,
  `gAccountingRecordId`, both `*WindowsRestartTime`, …). Filling them with sane defaults
  changed nothing, so this was **not** folded into the builder — the OEM seed is empty too.
* The seed covers ids 1-5, 12-20, 22-27, 30-35, 37-42 — **ids 6-11, 21, 28, 29 and 36 have
  no row at all** in the OEM schema. If the plug-in reads one of those by name it gets zero
  rows back. That is the most promising next lead.

### Next

1. `GvrPlusDEPlugin.dll` is mixed-mode C++/CLI (v1.1.4322) — decompilable. Find what it does
   after the `RestartMode` read.
2. Add a provider trace line for **zero-row** Fills, so a missing `GlobalVariable` name shows
   up immediately instead of looking like a successful query.
3. Only then revisit the game's own dongle check — the shell has to boot first.

---

## 2026-09-30 - applying the NFSU corpus; the JIT-dialog trap

Mined the NFSU findings for anything not yet applied to NASCAR.

### Applied from NFSU

* **`HKLM\SOFTWARE\Gvr\hercules` runtime tree** (both registry views). NFSU's *working*
  reference (`Reference_GVR_All.reg`) contains this whole tree with **per-application
  subkeys named after the executables** (`UNDERGROUNDGVR`, `UniverShell2`), plus
  `Boot\BootValue=2` and `Dongle\Inserted=0`. NASCAR logs `HerculesInit( GvrShell )` /
  `HerculesInit( AMPlayer )` and had **none** of it, so a non-elevated shell had its writes
  redirected into VirtualStore. Created the NASCAR equivalents: `AMPlayer`, `GvrShell`,
  `NASCAR_GVR`, `Dongle` (`Inserted=0`, `GameName=NASCAR`), the monitor keys, and
  `GVRCrashMonitor\NumApps=0` (we never launch the monitor stack, so no Prog entries).
  NFSU's own distinction holds: these are runtime config, **not** the startup/lockdown keys.
* **Cleared the VirtualStore shadows** (`HKCU\...\VirtualStore\MACHINE\SOFTWARE\[WOW6432Node\]Gvr`)
  - NFSU has a `Clear-GvrVirtualStoreRegistry` step for exactly this, and our own earlier
  runs had created them.
* Set `RestartEnable=0` on the AMPlayer hercules key (NFSU's UniverShell2 key carries the
  watchdog values; we have no monitor process to answer it).

### Ruled out from NFSU

* `DongleStorageDevice.dll` / `GVRSCR28` / `PCSCSCR2` / HASP DLLs: NFSU's shell probed for
  these repeatedly ("high-signal missing dependency"). The NASCAR trace shows **zero**
  probes for any of them, so the system32 GVR DLL set is not needed by this shell.
* Filling the 8 empty `GlobalVariable` values changed nothing (not folded into the builder).

### The trap: the CLR 1.1 JIT-attach dialog was faking progress

`HKLM\SOFTWARE\[WOW6432Node\]Microsoft\.NETFramework\DbgJITDebugLaunchSetting` was `16` with
`DbgManagedDebugger` = vsjitdebugger.exe. On an unhandled exception CLR 1.1 puts up a modal
*"Application has generated an exception that could not be handled ... OK to terminate,
Cancel to debug"* box - which the user hit, and which opens Visual Studio.

**That dialog is why the measured lifetime wandered between 3 s and 35 s: the process was
sitting on the message box, not making progress.** With `DbgJITDebugLaunchSetting=1`
(terminate, no prompt) the failure is consistent at **~0.9 s**. My earlier reading of
"3.6s -> 15.8s = the hercules keys helped" was therefore **not sound** - the hercules keys
are kept because they mirror the working NFSU reference, but they are not proven to fix
anything.

What *is* genuine progress from the CLR 1.1 pin: before it the trace stopped after the
**first** GlobalVariable query; after it, all **16** complete plus `HerculesInit`, in under
a second.

Also confirmed: WER never writes a dump and logs no APPCRASH, because the CLR's JIT-attach
path handles the exception before WER sees it. Registering a stand-in for
`DbgManagedDebugger` to capture the `EXTEXT "<exception text>"` argument the CLR passes did
not fire either - on that run the shell did not fault at all. **The crash is intermittent**,
which is the main reason it has been hard to pin down.

### Next

1. **x32dbg** (not installed on this box). A debugger parked on the process is the right
   tool for an intermittent first-chance exception in a 2007 mixed-mode plug-in: it gives
   the exception, module and stack on whichever run faults.
2. **Ghidra** on `GvrPlusDEPlugin.dll` + `PLUSDE.dll` (both mixed-mode; Ghidra reads their
   native halves) to explain what the debugger points at.
3. Provider now has an `AppDomain.UnhandledException` hook (logs type/message/stack to
   `gvrsqlite.log` under `GVRSQLITE_LOG=1`) - it is the only managed code we control in that
   process, so keep it.

---

## 2026-09-30 (later) - SHELL CRASH SOLVED: the missing GVR device layer

**`PLUSDE.dll` contains the literal string `DongleStorageDevice.dll`.** It loads that DLL
by name and calls its `CreateGVRStorageDeviceImp` export through `GVRStorageDevice.dll` -
the smart-card / dongle storage ABI the parent project already reverse-engineered in
`GVRSCR28_ABI_REPORT.md`. `GvrPlusDEPlugin.dll`'s own strings confirm the consumer side:
`GvrSmartDevice(GvrDataEngine*, char*, GVRSDType)`, `InitializeSmartcard`,
`GetSmartcardData`, `"Device initialization failed"`, `"Card read failed"`.

**None of that DLL set was ever installed.** Our installer staged only the MSVC 7.1
runtimes out of the disc's `WINDOWS\system32` file group. Deploying the rest app-local -

    GVRStorageDevice.dll  DongleStorageDevice.dll  GVRSDEmulator.dll
    GVRSCR28.dll  PCSCSCR2.dll  GVR_Resources.dll  Resources.dll
    UsbTrackerDll.dll  GVRInputRaw.dll  akshasp.dll  haspms32.dll
    csamsp.dll  MdmXSdk.dll

- changed the shell from "dies in ~1 s with 0xC000041D" to **staying up indefinitely, 3 runs
out of 3**. Working set 53 MB -> 77 MB.

Note `GVRSDEmulator.dll` exports the *same* two functions as `DongleStorageDevice.dll`:
they are interchangeable back-ends, and the emulator is GlobalVR's own **software**
implementation. That is the lever for running with no dongle hardware, and it is already
documented for NFSU in `CAREER_MODE_SMARTCARD.md` section 2b.

Installer: the DLL staging was initially put inside the big-copy branch, which meant
re-running on an existing install would not deploy it. Extracted into `Install-SupportDlls`,
which now runs on **every** invocation (idempotent) - the same reason the NFSU portable
installer stages its DLLs unconditionally.

### Where it stands: a hidden window, not a crash

The shell now initialises fully and **spins at 100% CPU with no visible window**. It does
create its windows - five of them, Anark's single-letter classes - and with `DisableGL=0`
the main one (`class 'A'`) is sized **-2,-2,1923,1083**, i.e. a full-screen 1920x1080 render
surface. All five are `IsWindowVisible = false`. So the front end is built and running but
never presents; something gates the show.

Ruled out on the way: `gCabinetIsRegistered=1` + `gOperatorFirstTimeInitilized=1` made no
difference, and there is no DB activity after init, so the spin is not a query loop.

Also found: **NASCAR and NFSU collide on `HKLM\SOFTWARE\gvr\Plus\1.1\Cabinet`** (the key is
versioned by Plus client, not by title). That live key currently holds a mix -
`PatchDownloadPath = D:\Games\NFSU\PatchService\` and `WebServerIP = 66.107.15.47` are
NFSU's, while `PlusSchemaPath`/`PublicKeyPath`/`PromotionPath` are ours. All values NASCAR
needs are present, but **our installer has repointed NFSU's schema path**, so the two
installs cannot currently both work. Needs a per-title switch or a backup/restore step.

### Next

x32dbg on the spinning process: a steady-state spin is a much easier target than the
intermittent crash was. Attach, pause, find the hot thread, read the stack - that names the
gate. Ghidra on `GvrPlusDEPlugin.dll` + `PLUSDE.dll` + `AMPlayer.exe` to map the addresses.

---

## 2026-09-30 (breakthrough) - THE CABINET FRONT END RUNS

The shell now boots to a **visible fullscreen 1920x1080 attract screen**. Three fixes, in
the order they were found:

1. **CLR 1.1 pin** (`Shell\bin\AMPlayer.exe.config`) - AMPlayer is a native CLR host, so
   with no config the shim binds CLR 2.0 and PLUSDE's C++ EH interop access-violates.
2. **The GVR device DLL set staged app-local** - `PLUSDE.dll` loads
   `DongleStorageDevice.dll` by name; none of that group was installed. Fixed the crash.
3. **`<GvrLinkingPlugIn />` removed from `GvrShellPlugInList.xml`** - this was the last one.

### How #3 was found (no debugger needed)

The shell was alive but pegged at ~100% CPU with a correctly sized fullscreen window that
was never shown. Rather than guess:

* 25 threads, one hot (`tid` with 53 s CPU, state Running), all others idle.
* Sampled its EIP via `Wow64GetThreadContext` - pinned at `ntdll+0x79EEC`, which resolves
  (from a parse of SysWOW64\ntdll.dll's export table) to **`NtDelayExecution` + 12**, i.e. a
  `Sleep()` hammered in a tight poll loop.
* Read ESP and scanned the stack for return addresses inside loaded modules
  (`GetThreadContext` + `ReadProcessMemory` from **32-bit** PowerShell - 64-bit PS cannot
  enumerate a WOW64 process's 32-bit modules). Top frames:
  `GvrLinking.dll +0x1068`, `GvrLinkingPlugIn.dll +0x10B4`.

`GvrLinking.dll` is the **cabinet-to-cabinet linking** layer (`IsOnline`,
`GetNumCabinetsOnline`, `GetCabsAvailable`, IPX/UDP sockets - the UDP ports
`gvrNetworkConfig.bat` opens). On a standalone install it polls forever and the front end
never presents. Removing the plug-in: CPU ~100% -> ~4%, working set 85 MB -> 116 MB (attract
content loads), and the window is shown.

`GvrMotionPlugIn` is removed with it (Tsunami motion base, see `tsunami.reg`). Both are now
handled by `Disable-CabinetPlugIns` in the installer, which keeps the OEM file as
`GvrShellPlugInList.xml.oem`.

### Useful incidental findings

* AMPlayer's own option parser (`FUN_63b19300` in Ghidra, image base `0x63B00000` which
  matches the live module base exactly) recognises:
  `-nodebug -nocursor -cabinet -log -help -t -test -data -testInfo`.
  **`-v` is not in that list** yet it demonstrably works (`-v FULL` -> 1920x1080 window,
  without it -> 1720x832), so it is parsed elsewhere in the Anark core.
  `-help`/`-testInfo` set the mode field at `+0x654` to 3/5, which makes
  `FUN_63b249e0` skip window creation and `ShowWindow` entirely - a headless mode, and a
  trap to remember if the window ever goes missing again.
* The OEM command line is `AMPlayer.exe shell.am -v FULL -cabinet` (from `HerculesCab.reg`);
  we had been dropping `-cabinet`. It is not required for the window to appear, but it is
  the cabinet's own invocation, so the launcher should pass it.

### Next

* Pass `-cabinet` from `NascarLaunch.exe`.
* Drive the front end: does Start (`s`) reach track/car select and launch the race? That is
  where `NASCAR_GVR.exe`'s dongle check finally matters.
* Still open from earlier: NASCAR and NFSU collide on `HKLM\SOFTWARE\gvr\Plus\1.1\Cabinet`.

---

## 2026-09-30 (game side) - ROOT CAUSE: GvrIO teardown deadlock, not the dongle

The game's stall is fully diagnosed, and the dongle is **not** involved - the game never
reaches its dongle check.

### The deadlock

`NASCAR_GVR.exe+0x58D00` calls `cGvrIO::~cGvrIO`, whose first act is (Ghidra, GvrIO.dll at
its preferred base 0x10000000 = the live base, so addresses map 1:1):

    1000257A  MOV byte [1000C02C],0      ; clear the ack flag
    10002581  PUSH 1
    10002583  CALL ESI                   ; Sleep(1)
    10002585  MOV AL,[1000C02C]
    1000258A  TEST AL,AL
    1000258C  JZ 10002581                ; spin until the worker acks

`0x1000C02C` is a **per-iteration heartbeat**: the worker thread created at the end of
`cGvrIO::Initialize` sets it at `0x10002336`, immediately before its own `Sleep`. So the
destructor means "wait until the worker has gone round once".

**There is no worker thread.** A full thread enumeration of the stalled game shows 7
threads: 4 system thread-pool (`ZwWaitForWorkViaWorkerFactory`), 1 CoreMessaging/inputhost,
1 game thread sleeping, and the main thread parked in this loop (`ZwDelayExecution`).
Nothing in GvrIO. So the destructor waits forever for a heartbeat from a thread that is not
running, and the game hangs during init at ~15 MB and 0% CPU.

### Proof, and what lies past it

Patched a copy of GvrIO.dll - `JZ` -> `NOP NOP` at RVA 0x258C (`74 F3` -> `90 90`) - so the
wait falls through after one Sleep. Result: **the game stops hanging.** It proceeds into
`Game::CreateManagers` and for the first time writes a real log line:

    2026/9/30, 21:40:53
    LOADTIME: T= 0.005, delta= 0.005 : Game::CreateManagers...

then dies with `0xC0000005` inside ntdll heap code. So the NOP is *not* the fix - removing a
synchronisation point is exactly the blanket-stub mistake `shell-oem-abi-forwarding` warns
about - but it proves the deadlock is the blocker and that there is a working game behind it.

### Ruled out

* **Not the dongle.** The fatal dongle check is never reached.
* Not the plug-ins: removing `Dongle.dll` and `Steering.dll` (GvrIO loads `<device>.dll`,
  `Dongle.dll`, `CardDispenser.dll` by name via `FUN_10006500`) changes nothing. Note
  `CardDispenser.dll` does not exist in the payload at all, and that is harmless - a failed
  `LoadLibraryA` leaves the loader's return value true.
* Not stdout/OutputDebugString: the game emits nothing on either (verified with a
  DBWIN_BUFFER listener that captured a self-test successfully).

### Device-type map (from FUN_10006500)

`GVR_DEVICE_TYPES`: **3 = Trackball, 4 = Joystick, 5 = Steering**, overridable from
`GvrIO.xml` (`if (0 < cfg[0x10]) type = cfg[0x10]`). Plug-in ABI confirmed:
`id = Initialize(GvrIOMessageSend, hwnd)`, plus optional `MessageCallback`, `Finalize`.

### Next: a GvrIO.dll shim (the NFSU pattern)

Forward all 11 exports to `GvrIO_oem.dll` (a .def file can forward C++ mangled names
verbatim, so most need no code) and override only what is needed so teardown cannot
deadlock - either keep a live worker or make the destructor's wait bounded. Then re-test and
see whether `Game::CreateManagers` gets further; the heap AV may simply be a consequence of
the crude NOP.

---

## 2026-09-30 (game internals) - GvrIO shim built; main() and the dongle gate decoded

### 1. The GvrIO deadlock is FIXED - `src\GvrIOShim`

A drop-in `GvrIO.dll`: **9 of the 11 exports are plain forwarders** to `GvrIO_oem.dll`
(`.def` forwarders, so OEM code runs untouched), plus two guarded overrides. Verified
against the OEM export table: 11 names, none missing, none extra.

* **`~cGvrIO` override.** The OEM destructor is only safe on a fully initialised object: it
  waits for the worker thread's heartbeat (created at the *end* of `Initialize`), calls
  `DeleteCriticalSection` on a CS the worker initialises (RVA 0x226B), and deletes six
  globals `Initialize` populates. `NASCAR_GVR.exe` constructs a `cGvrIO` and destroys it
  **without ever calling Initialize** - so the shim skips the OEM teardown in that case.
* **`Initialize` override** - calls the OEM one, then reports success so the game cannot
  decide to tear `cGvrIO` down. (Not actually exercised: see below.)
* **Heartbeat watchdog** - supplies the missing ack only after it has been absent 120 ms, so
  a live worker always wins.

Result: hang -> heap AV -> **clean, deterministic exit**. Progression of the game's own log:
nothing -> `Game::CreateManagers...`.

`.def` gotcha worth remembering: a mangled C++ name on the LEFT of `=` works for a
forwarder, but `name = MyFunc` truncates the export at the first `@` (we got `?Initialize`,
and the game refused to start with "the procedure entry point ... could not be located").
The fix is to declare the real C++ class shape so the **compiler** emits the decorated name
and `__thiscall`:

    namespace Gvr { namespace Zeus { namespace GvrIO {
        struct sMessage; enum GVR_DEVICE_TYPES { };
        class cGvrIO { public: __declspec(dllexport) bool Initialize(GVR_DEVICE_TYPES, HWND__*, void*(__cdecl*)(sMessage*)); ... };
    }}}

### 2. `HWInput::Setup` (FUN_0045bbb0, hwinput.cpp)

The only caller of `cGvrIO::Initialize`, and it is **conditional**:

    if (DirectInput8Create(...) == DI_OK)
        if (obj[1] == 0 && DAT_0078e354 != 0)              // DAT_0078e354 = the game's HWND
            Initialize(this, 5 /* Steering */, hwnd, &LAB_00456f80);

So `Initialize` is skipped until the game has a window - which is why the shim's override is
never reached during startup. Normal, not a fault.

### 3. `main()` = FUN_0045dc80 - the whole startup, and the dongle gate

    SetUnhandledExceptionFilter(FUN_0045dc10)
    parse "+heapmegs=", FUN_0045d240(cmdline)
    if (FUN_00447790() != 0)   FATAL "Unable to create resource managers", _exit(0)
    ...
    if (!FUN_004880e0(...))    FATAL "Unable to configure the game"     // logs, does NOT exit
    ... +allowmultiple / +altosc / +noPlayerInGame / +gentelem / +ForceReplayRecord
        / +showSounds / +showStreams ...
    "Starting stall monitor...", FUN_00694850("NASCAR_GVR"), FUN_006946c0(45000)
    bVar1 = true
    if      (!FUN_006721d0())                       FATAL "NO DONGLE DETECTED"
    else if (!FUN_006721f0())                       FATAL "INVALID DONGLE DETECTED"
    else if (stricmp(FUN_00672220(), "NASCAR") == 0) { log "DONGLE found: ..."; bVar1 = false }
    [ENTER]  if (FUN_00448960() != 0) FATAL "failed the ENTER state"
    [SETUP]  if (FUN_00448d40() == 0) {
                 if (!bVar1) { [INIT] [RESTART] ... run the game ... }   // <<< gated here
             } else FATAL "failed the SETUP state"
    shutdown, "~ fini ~"

**`bVar1` is the dongle gate**: without a present + valid dongle whose game name is
`"NASCAR"`, the entire INIT/RESTART/play sequence is skipped and the game walks straight to
shutdown. No crash - it simply declines to run.

#### The dongle, fully decoded

* `FUN_006721d0` (present) and `FUN_006721f0` (valid) both reduce to **`FUN_00672460(0)`**.
* `FUN_00672460(i)` zeroes a **27-byte record** at `0x00779CC0 + i*0x1B`, calls
  `FUN_00672280` (the hardware read) and on success sets `record[0] = 1`.
* `FUN_00672280` talks HASP: `FUN_00672525(service, 300, port, pass1, pass2, ...)` with
  services **1 (ISHASP), 5 (HASPSTATUS), 0x32 (READBLOCK)** - and the HASP code is
  **statically linked** (`FUN_00672525` -> `FUN_00674975`), so there is no DLL boundary to
  shim.
* Record layout: `+0` present flag, **`+1` game name** (`FUN_006724f0` returns
  `&DAT_00779cc1`, NUL at `+7`, so `"NASCAR"` fits exactly), then version / region / cab
  strings at `+8`, `+0xC`, `+0x10`/`+0x15`. Getters: `FUN_00672220` name,
  `FUN_00672200`/`240`/`260` the others (empty values only produce warnings).

A 36-byte stub at `FUN_00672460` satisfies all three conditions (present, valid, name):

    C6 05 C0 9C 77 00 01                 mov byte  [779CC0], 1
    C7 05 C1 9C 77 00 'N','A','S','C'    mov dword [779CC1], "NASC"
    66 C7 05 C5 9C 77 00 'A','R'         mov word  [779CC5], "AR"
    C6 05 C7 9C 77 00 00                 mov byte  [779CC7], 0
    B0 01                                mov al, 1
    C3                                   ret

(The function body is 83 bytes, so it fits.) Per `nfsu-launcher-and-settings` this belongs in
the launcher as a **suspended-process memory write**, not a file patch - nothing on disk
modified. A patched copy (`NASCAR_GVR.dongle.exe`) exists only for testing.

### 4. Where it actually stops now

`LOADTIME.txt` gets the *opening* `Game::CreateManagers...` and never the closing
`...Game::CreateManagers`, so an **unhandled exception is thrown inside CreateManagers** -
before the dongle check is ever reached (the dongle patch is correct but premature).

The filter explains the exit code:

    FUN_0045dc10:  log "*** BADSTUFF: Unhandled Exception! *** ExpCode/ExpFlags/ExpAddress"
                   _exit(-1)

and the game's log target (`FUN_00443000` -> `FILE* DAT_0078fd00`) is **never opened in the
release build**, so that message goes nowhere and WER sees nothing (the filter exits before
it). Hence: silent `0xFFFFFFFF`, no event, no log.

The unwind passes through the HWInput destructor (`FUN_00458d00`, frees 63 pointers then
`~cGvrIO`), so the throw is in or under the HWInput construction chain -
`FUN_0045f040` (a 0x9A14-byte object) is pure field init plus
`FUN_00463ea0` / `FUN_0045e9d0` / `FUN_00466ba0` / CRT calls.

### Next

x32dbg on `NASCAR_GVR.exe` (first-chance exception during CreateManagers) gives ExpCode +
ExpAddress directly; the EXE's Ghidra base 0x400000 equals its live base, so the address maps
1:1 to a function we can decompile immediately.

### Deliberately not doing

Anything in `NASCAR_INSTALL_ANALYSIS.md` §2–3: no cabinet autostart, no monitors, no
firewall/DHCP/volume/vibrance changes, no event-log policy edits, no cursor scheme
replacement, no motion base, and never the recovery disc's `DISKPART.TXT` (it wipes
disk 0).

---

## 2026-09-30 (later) — THE GAME RUNS. Race confirmed at Daytona.

`NASCAR_GVR.exe` loads a track, runs the physics and hands out a HUD. User-confirmed:
"well it works i was able to play it". Three separate blockers, found in this order.

### 1. The CreateManagers crash was a mangled directory name

x32dbg (MCP on port 3032) caught the first-chance exception at **0x0048B990**:

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

The manager's directory table (`+0x1C0 "CreateACar\"`, `+0x1D0 "__CreateACar\"`,
`+0x1E0 "Basic Settings\"`, `+0x1F0 "UI Elements\"`) holds names **with spaces**, and the
enumeration found nothing because the extracted tree had `Basic_Settings`. When the search
comes back empty the engine stores NULL and dereferences it unconditionally - a genuine
engine bug that the cabinet never hit, because on a cabinet the files were always there.

**Cause:** `unshield` rewrites DIRECTORY names on extraction (spaces -> underscores) while
leaving FILE names alone; `Extract-NASCAR-Sources.ps1` did not pass `-R`. Exactly 7 directory
names are affected, taken from the cabinet's own directory table, never guessed - every other
underscore in the tree (`END_OF_SEASON`, `MAIN_MENU`, `PP_SOT`, `Career_NEXTEL`...) is real:

    2004 NNS   2005 NEXTEL Cup   Basic Settings   New Hampshire
    Pit Elements   Tab Backgrounds   UI Elements

**Fix:** `Tools\Fix-GvrDirNames.ps1` (idempotent, deepest-first, 69 renames). Wired into
`Extract-NASCAR-Sources.ps1` after extraction and into the installer as `Fix-DirNames`, so
old payloads are repaired too. We deliberately do NOT extract with `-R`: that would also turn
the staging container `File_Group` into `File Group` and break every path the installer
resolves.

**Verified:** diffing the repaired tree against `unshield l data1.cab` accounts for
**4780 of 4791** entries; the 11 remaining are `InstallGVR\*`, the cabinet-installer payload
we never deploy. Before the fix: 709 missing.

### 2. The HASP dongle gate, stubbed in the GvrIO shim

Ghidra confirmed the gate reduces to one function:

    FUN_006721d0 / FUN_006721f0 -> FUN_00672460(0)   ; present / valid
    FUN_006724c0(0)             -> record[0]          ; the present byte
    FUN_006724f0(0)             -> &record[1]         ; the game name
    FUN_00672220()              -> present ? &record[1] : 0
    main gate: present && valid && stricmp(name,"NASCAR") == 0

`FUN_00672460(idx)` zeroes a 27-byte record at `0x00779CC0 + idx*0x1b`, lets the statically
linked HASP services fill it, and on success sets `record[0]=1`. The whole gate is satisfied
by **39 bytes** over that function: write 1 to `record[0]`, `"NASCAR\0"` to `record[1..7]`,
return 1. The real function is 83 bytes (0x00672460..0x006724B2, then `CC` padding), so
nothing after it is touched.

Applied from **`GvrIOShim`'s DllMain**, not by editing the executable: the shim is already
mapped into the game before `main()` runs, so `NASCAR_GVR.exe` stays byte-identical on disk
and every launcher (Anark shell, NascarLaunch, a debugger) behaves the same. Guard rails: the
original 16 bytes are verified first (refuse on mismatch), the record address is computed
from the real load base, and `GVRIOSHIM_NO_DONGLE_PATCH=1` disables it for a real dongle.
Export table re-verified after the rebuild: 11/11, none missing or extra.

Result in the game's own log: `DONGLE found: game=NASCAR, version=, region=, cab=`. The empty
version/region/cab produce two warnings ("not a valid dongle region code / cabinet type") -
harmless so far, but see Next.

### 3. The game is not a standalone executable - it needs a track

With the dongle satisfied, a bare launch got through Direct3D and then:

    Game::GvrEnter
    game.cpp 3218: Warning : ** NO TRACK SPECIFIED
    **FATAL** Game failed the ENTER state

`NASCAR_GVR.exe` is only ever started by the front end. Arguments are `-track <name>` and
`-series <name>`; the OEM registry `cmd` is `-startPos 43 ` and the shell appends the track.
**The game's ArgManager splits on whitespace and does not honour quotes**, so a series name
containing a space cannot be passed on the command line (`-series "2005 NEXTEL Cup"` arrives
as `'"2005'`). `2005 NEXTEL Cup.gdb` is flagged `Series Default Filename = true`, so omit
`-series` and let it default.

Working launch, user-confirmed playable:

    NASCAR_GVR.exe -track Daytona -startPos 43

Tracks: `Bristol`, `Daytona`, `Indianapolis`, `Lowes`, `Phoenix`, `Talladega`.

### The game logs everything - it always did

Earlier note in this file ("the game's log target is never opened in the release build") was
**wrong**. `FUN_004430e0()` at the top of `main()` opens `LOG\trace00N.txt`, so the game
writes a full startup trace - state banners, LOADTIME timings, SystemInfo, every
`SetError FATAL!`, and `*** BADSTUFF: Unhandled Exception! ***` with ExpCode/ExpAddress. It
only looked silent because the OEM tree ships no `LOG` folder and the engine will not create
one; `Install-Payload` already creates it, which is what switched the logging on. **Read
`Game\LOG\trace*.txt` first from now on** - it is far faster than the debugger.

`OutputDebugString` really is near-silent: the only message is `ACTIVATE`.

### Non-fatal asset gaps (pre-existing, not our damage)

The cab diff is clean, so these are OEM content gaps and the game races straight past them:
`Daytona Load04.jpg` (only Load01-03 ship), `2005-DAYTONA500.clf` (Daytona and Talladega have
no `.clf`; Bristol/Indy/Lowes/Phoenix do), and `POPUPQUIT0000.TGA` / `POPUPHEADER.TGA` /
`VEHICLEDISPLAY.TGA` (in neither the install nor the cabinet).

### Next

1. **Controls.** User: "the controls are very different, the numpad is pretty much the screen
   view of the car" - i.e. the engine's default keyboard map, not a driving map. The binding
   sets are `Options\Basic Settings\*.CTL` (`Keyboard.CTL`, `Default Wheel.ctl`, three wheel
   presets) and the live set is `Save\GvrSinglePlayer\GvrSinglePlayer.CTL`.
2. Drive the Anark shell end-to-end so it launches the race itself (the actual cabinet flow);
   the registry `cmd`/`launchFolder`/`launchName` plumbing is already installed.
3. Consider filling the dongle record's version/region/cab fields to clear the two warnings.
4. `DbgJITDebugLaunchSetting` is still **1** (original 16) from this debugging session.

---

## 2026-10-03 — cabinet flow traced end to end; one blocker left, and it is well defined

Goal: make the install behave like the cabinet — the shell drives attract, selection and the
race handoff. Ran the real path and traced it. **The shell is healthy and the chain is complete
except its own dongle back end.**

### What the end-to-end run proved

`NascarLaunch.exe` → `AMPlayer.exe "Shell.am" -v FULL`, with every child process monitored for
240 s:

* shell boots healthy: 118 MB, 29 threads, responding — matches the known-good attract state
* GVR event log: `HerculesInit( GvrShell )`, `HerculesInit( AMPlayer )`
* registry carries `launchFolder`, `launchName`, and
  `cmd = -startPos 43 -windowed -width 1280 -height 720 -noabort -notimeout`
* **`NASCAR_GVR.exe` never spawned; no new `trace00N.txt`** — confirmed twice, by process
  monitoring and by the filesystem (the game writes a trace on every launch)
* shell reported the dongle missing/invalid and exited via `DongleError.am`

So the launcher, CLR 1.1 pinning, plug-in manager, Hercules init, app-local DLL set, database
wiring and registry plumbing all work. Only the shell-side dongle answer is missing.

### Architecture, now named rather than assumed

Full detail in `NASCAR_FINDINGS.md` §9. The short version:

* **`AMPlayer.exe` IS GvrShell** — build paths `c:\dev\zeus\shell\gvrshell\...`, dialogs say
  "GvrShell", event log says `HerculesInit( GvrShell )`. There is no separate missing GVR shell
  binary; everything the cabinet ran is present.
* **Its image base is `0x63B00000`, not `0x400000`.** Ghidra and x32dbg addresses only agree if
  the base matches — the single easiest way to waste an afternoon here.
* `GvrGameLaunch` is the handoff. Entry points: `In GvrGameLaunch` @ `0x63DBCE40`,
  `ERROR: %s trying to launch %s in %s` @ `0x63DC0D5D`, `Launch` @ `0x63DC4F80`.
* **The `.am` scenes are packed** (magic `C3 0B 0A 00`), which is why `launchName` /
  `launchFolder` appear in no binary — the scene supplies the value name to
  `Operator_GetRegistry` at runtime. The registry plumbing is real.
* **`NASCARPlugIn.dll` is the operator menu**, not the game launcher — coin service, pricing,
  credits, timezone, propagate. Its one `CreateProcessA` is `_runRegistrationProgram`
  (db_propagate), **not** the game handoff. Easy to misread.
* It is also full of `GvrPlusConstant.*_PGA4` types, confirming the shell derives from the
  **PGA golf** codebase — which explains the dead `C:\NASCAR_TOUR_GOLF_2006\...` paths.

### The remaining blocker

A **second, independent** dongle check, on the shell side. The game-side gate solved on
2026-09-30 lives inside `NASCAR_GVR.exe` and is satisfied by the 39-byte stub in `GvrIOShim`,
which verifies the host module's bytes and so deliberately does not touch `AMPlayer.exe`.

The shell instead goes `Operator_GetDongleCabinetId` → `GvrSmartDevice(engine, gameName,
GVRSDType = 2)` → `GVRStorageDevice` → **`DongleStorageDevice.dll`** (managed, HASP statically
linked) → `hasp(6, 300, …)`. Decompiled; the verdict logic is four lines:

```c
IsPresent() : two password pairs, hasp(6,...); true when (p3 == 0 && p1 != 0)
GetType()   : return state[+16] ? 2 : 0      // 2 = DONGLE, only once HASP was found
GetSize()   : 112                            // HASP memory image size
Read()      : 5000 unless state[+16]; else hasp(50,...) in 16-bit words
```

No hardware → `IsPresent` false → `GetType` 0 → not a dongle → `DongleError.am`.

### Decided fix, and the trap avoided

`DongleStorageDevice.dll`, `GVRSDEmulator.dll` and `GVRSCR28.dll` export the **same two
functions** behind the same front end, so they are interchangeable back ends — a drop-in
replacement is the sanctioned fix, no patching.

**But do not just copy `GVRSDEmulator.dll` over `DongleStorageDevice.dll`.** Checked before
trying: its `GetType()` returns **0 (PLAYER)**, so the shell would see a blank player card, not a
dongle; its `GetId()` never writes its out-param, and its image is zeroed each run. It is the
best *reference* for the vtable (`GVRSCR28_ABI_REPORT.md` §D documents every slot), not a
drop-in.

A correct replacement needs `GetType()` → 2, `IsPresent()` → true, `GetSize()` → 112, a stable
`GetId()`, and file-backed `Read`/`Write` over a 112-byte image whose contents satisfy
`GvrSmartDevice.ReadHeader` (schema-driven, ABI report §B.3). Ship as `DongleStorageDevice.dll`
with the OEM kept as `DongleStorageDevice_oem.dll`.

### Next, in order

1. Confirm with x32dbg on `AMPlayer.exe` that the shell gates **only** on this presence call —
   breakpoint the `hasp(6, …)` path — so the replacement is built against measurement rather
   than inference. Remember the `0x63B00000` base.
2. Build the drop-in back end.
3. Then re-run the end-to-end test and drive attract → select → race → return.

New decompiled artifacts: `NASCAR\Decompiled\NASCARPlugIn\` (40,507 lines),
`NASCAR\Decompiled\DongleStorageDevice\` (636 lines).

## 2026-10-03 - Autostart and OEM-install registry fully mapped

New doc: `NASCAR_AUTOSTART_AND_INSTALL_REGISTRY.md`.

- `Extracted\RecoveryImage` is a **complete** extraction of the recovery WIM (4684/4684 files by
  path and size; all 22 hive files byte-identical to the WIM's). No re-extraction needed.
- Recovery image autostart: `Run` = HideTaskbar + netset + audio vendors; `RunOnce` =
  `CDS.exe /t:10`; `SetupExecute` = setupcn/setupcl (XPe re-seal); autologon as `cabinet` on a
  stock Explorer shell; the `cabinet` user carries 22 Explorer lockdown policies (previously
  missed in `NASCAR_REGISTRY.md`). No GlobalVR service.
- OEM install: `setup.inx` imports **every** `Registry\*.reg` (nine files). The front end's
  autostart is Hercules' `GVRCrashMonitor` `Prog04` (`AMPlayer.exe shell.am -v FULL -cabinet`),
  launched by `Run\GVRHercules`; `Shortcut.wsf` adds `LoadingShell.lnk` + `HideTaskbar.lnk` to the
  All Users Startup folder (previously described as only a Close Apps shortcut).
- `PGA.msi` has no Registry table; its `GvrPlusExportDatabaseScript` writes
  `HKLM\SOFTWARE\Gvr\Installer\DeskTopEngine\Exists=1` (present => next run is an upgrade:
  SQL restart + `db_propagate -recover`), `UnregDlls` regasm/gacutil's the five `GvrPe*` DLLs,
  `DeleteRegistry` removes `Gvr\Installer` on uninstall.
- `Hercules\HerculesSetup.reg` and `stopall.bat` are PGA Tour Golf leftovers and are never imported.

---

## 2026-10-03 (cont.) — shell dongle self-test fully traced; two routes defined

The shell blocker is now completely understood (full detail: NASCAR_FINDINGS.md section 10).
Summary of the day's second half:

* The error is a `Shell.am` power-on **self-test** showing `Game/Version/Region` from the
  dongle; empty -> "DONGLE MISSING OR INVALID".
* All three accessors (`IsValidDongle`, `GetDongleID`, `GetDongleData` in `GvrPlusDEPlugin.dll`)
  gate on **`GvrSmartDevice.DongleInserted()`** in `PLUSDE.dll`, which returns false WITHOUT
  reading the storage device (verified: 0 interrogation calls on a logging software device).
* The fields come off real dongle hardware; `Header.GameName` is defined nowhere on this box.
* External corroboration: the NASCAR dongle holds `NASCAR1.5CTRYXX_CAB0`, which satisfies the
  `strstr(GameName,"NASCAR")` check and matches `RUNTIMEOEMVERSION 1.5.0.04`.

Eliminated (do not repeat): stock-emulator swap, GetType->2, DB dongle rows, GlobalVariable
CabinetId, and patching IsValidDongle alone. The SQL trace proved the check never touches the DB.

### Routes

1. Patch the `PLUSDE` chain upward from `DongleInserted`. Risk: PLUSDE is the shared mixed-mode
   DB core; number of layers unknown; each patch is in the assembly the SQLite backend relies on.
2. **CHOSEN: content reconstruction.** Build a `Header.*` image the UNPATCHED
   `DongleInserted`/`ReadHeader` accept, served through our `GVRSCR28.dll` software device, so the
   self-test passes on correct data rather than removed checks. More faithful to a real cabinet
   and more likely to terminate (real contents satisfy every check at once). Needs the dongle
   header layout, which the ABI report flags as not yet recovered — recovering it is the next
   task, by reading `DongleInserted`/`ReadHeader`/`GetMemObject` to see exactly what bytes they
   parse and where.

### Keepers regardless of route
* `Tools\Provision-GvrDongle.py` (schema-driven, reusable)
* `GvrSqlite.cs` log-path fix (honours GVRSQLITE_LOG as a path, %TEMP% fallback — genuine bug)
* `NASCAR\src\GvrDongleEmu\` (software GVRStorageDevice back end; correct, gate just upstream)
* `NASCAR\Tools\Patch-GvrDongleCheck.ps1` (managed boolean-gate patcher via dnlib NativeWrite)

### Reversible state left on disk
patched `GvrPlusDEPlugin.dll`, software `GVRSCR28.dll`, `PCSCSCR2.dll` (all have `_oem` backups).

---

## 2026-10-03 (night) — THE SHELL RUNS: attract, car select and operator menu reached

User-confirmed: "NASCAR Start Your Engines" attract, `NASCAR_Selection.am`, operator menu. Full
detail and addresses: NASCAR_FINDINGS.md section 11. The previous entry's "content
reconstruction for PLUSDE" route is **abandoned** — the self-test does not use PLUSDE.

### The chain of blockers, in the order they appeared
1. **DONGLE MISSING OR INVALID** — the reader is the GvrIO plug-in `Shell\bin\Dongle.dll` (same
   HASP record library as the game). Patched its record filler with `Tools\Patch-DongleDll.py`,
   image `NASCAR1.0CTRYXX_CAB0`. The scene demands version **1.0**; `1.5` gives "Incorrect Game
   Version".
2. **"Please Wait..." forever** — `NASCARPlugIn.dll` silently failed to load: it imports the debug
   runtime `MSVCP71D.dll` (+ `MSVCR71D.dll`). Staged both from the disc's `WINDOWS\system32`.
3. **The PC's time zone changed** to Turkey Standard Time the moment the plug-in loaded.
   Restored the user's zone (Romance Standard Time) and neutralised the import
   (`SetTimeZoneInformation` → `GetTimeZoneInformation`).
4. **Black screen** — `Stats_Update_UpTime` does `DateTime.Parse("")` on the unseeded
   `UpTimeStart`; a `FormatException` every frame re-runs case 4 forever. Seeded
   `UpTimeStart`/`UpTimeEnd`.
5. **"There is a CD in the drive"** — the check is `CreateFileA("D:\")`; patched to `?:\`.
6. **"Error detected with GVRIO board"** and **"Calibrate the accelerator and brake"** — added a
   `GvrIOMessageSend` overlay to `GvrIOShim` (IO board 3.03; axis range 0..255 when no device
   answers) and deployed it as the shell's `GvrIO.dll`.
7. Also disabled `Shell\GammaSet.exe` (case 4 runs it with gamma/vibrance arguments).
8. **~4 fps** — `HerculesPlugIn` blocked the main thread in `WaitForSingleObject` (1 s timeouts)
   waiting for `GVRCoinMonitor`, which is not running. Patched its wait helper to a zero-timeout
   poll (3 bytes at file offset `0x4904`). Moving the shell to the NVIDIA GPU (Windows GPU
   preference) was tried first and did not help on its own.
9. **Some operator menus still slow** — `NASCARPlugIn.dll` has its own copy of the Hercules
   client; its `MemoryConnection::Receive` waited 1000 ms per query. Zeroed its receive timeout
   (file offset `0x2D0AC`).

### How it was found (reuse this)
* Inflate the `.am` zlib streams (`Tools\Extract-AmScripts.py`) — the scene JavaScript names every
  check. This alone would have saved most of section 10.
* `Tools\AKLogShim` (stand-in for Anark's unshipped `AKLog2` logger + vectored exception logger)
  with `Tools\dbwin.py` showed the per-frame `.NET` exception that the script swallowed.
* Registry side effects land in the VirtualStore (the shell is unelevated and virtualised) — they
  show how far the script got.

### Ruled out tonight (do not repeat)
* Deploying the software dongle as `GVRSCR28.dll` **or** `DongleStorageDevice.dll` — the
  self-test never creates a PLUSDE device. (The software `DongleStorageDevice.dll` is still on disk;
  `NASCARPlugIn`'s `Operator_Init` reads a blank header from it and copes.)
* `DebugMode` in `HKCU\Software\Anark\Client\3.0\Preferences` alone — logs nothing.
* "The main thread is hung" — it idles in the message pump; the scene runs on CController.

### State on disk (`D:\Games\NASCAR`) — all reversible, OEMs kept
* `Shell\bin\Dongle.dll` (patched; `Dongle_oem.dll`)
* `Shell\bin\msvcp71d.dll`, `msvcr71d.dll` (staged from disc)
* `Shell\bin\plugins\NASCARPlugIn.dll` (TZ import + CD path + receive-timeout bytes;
  `NASCARPlugIn_oem.dll`)
* `Shell\bin\GvrIO.dll` = GvrIOShim with the overlay (`GvrIO_oem.dll`)
* `Shell\GammaSet.exe.arcade-disabled`
* `Shell\bin\plugins\HerculesPlugIn.dll` (zero-timeout waits; `HerculesPlugIn_oem.dll`)
* `HKCU\Software\Microsoft\DirectX\UserGpuPreferences`: `AMPlayer.exe`, `NASCAR_GVR.exe` →
  `GpuPreference=2;`
* `Shell\bin\AKLogShim.dll` is on disk but its COM registration was removed (re-add to use it)
* `Shell\bin\AKLogShim.dll` + `HKCU\Software\Classes\CLSID\{DE411D6F-2BDA-444C-AC87-9171BAFD9E99}`
  (diagnostic; safe to leave, delete the key to remove)
* VirtualStore `...\GlobalVR\NASCAR`: `UpTimeStart`, `UpTimeEnd` (now maintained by the shell)
* Still from earlier sessions: software `DongleStorageDevice.dll`, `GVRSCR28.dll`, `PCSCSCR2.dll`,
  patched `GvrPlusDEPlugin.dll` — none needed for the fixes above; candidates to revert.

### Next, in order
1. Repoint `c:\NASCAR\Shell\...` absolute paths in `NASCAR_Selection.am` (car skins, driver
   names, `Audio`) — missing art/music in car select.
2. Start a race from the shell and confirm the hand-off to `NASCAR_GVR.exe` (`launchFolder` /
   `launchName` / `cmd`).
3. Fold everything into `Install-NASCAR-GVR-Portable.ps1`: stage the debug runtimes, seed the
   uptime values (HKLM, elevated), apply the two `NASCARPlugIn` byte patches and the `HerculesPlugIn` wait patch, patch `Dongle.dll`,
   deploy the shell GvrIO shim, disable GammaSet. Rebuild the game's `GvrIO.dll` from the same
   source so both copies match.
4. Decide what to do about `rundll32 nvcpl.dll,dtcfg setmode` (case 11) on old NVIDIA drivers.

---

## 2026-10-04 — operator menus and car select fixed; a full race from the shell

User-confirmed: a race launched from the shell ran and returned (`trace010.txt`, `~ fini ~`);
operator Info loads and reads "Region: United States"; car select shows the drivers. Detail:
NASCAR_FINDINGS.md **section 12**.

### What was wrong, in the order it surfaced
1. **Operator Info stuck on "Loading. Please Wait..."** — the page reads `Gvr.MacAddress`, which
   only the removed `GvrLinkingPlugIn` provides. Cell → static "N/A" (`MachineInfoCol.xml`); the
   four `NASCAR_GET_CONTACT_HEADER` cells in `SysStatusCol.xml` (provided by nobody) → blank.
2. **Car select only showed "Start Your Engines" and auto-picked a car after 24 s** — three causes:
   * absolute `c:\NASCAR\Shell\Textures` / `Audio` paths in `NASCAR_Selection.am` →
     `Tools\Patch-AmPaths.py` (relative: textures to the `.am` folder, music to `Shell\bin`);
     menu music returned;
   * the cabinet read as **international** (country `"XX"`) because the leftover software
     `DongleStorageDevice.dll` answered the PLUSDE header read; international mode removes four
     drivers and that code errored every frame → OEM `DongleStorageDevice.dll` restored
     (country stays `"US"`);
   * **`CabinetComm.sendPacket()` → `Gvr.Call("SendPacket")`** (linking plug-in only) silently
     aborted `resetDrivers → updateDriver` every frame → removed in all 17 scenes with
     `Tools\Patch-AmCalls.py`.
3. **Steering**: `Gvr.AxisX/Y/Z` = GvrIO message 5; the shell shim now answers it (centred; arrow
   keys / D / G steer while the shell has focus) when no analog device exists.

### The lesson worth keeping
A `Gvr` method or property that no loaded plug-in provides **aborts the script chain silently** —
no exception, no log. After any plug-in-list change, run `Tools\Find-AmMissingCalls.py` and fix
what is reachable. Checkpoint `output()` lines inserted with `Tools\Edit-AmScript.py` find the
abort point in one run.

### Ruled out
* The renderer — Anark's right-click "Rendering Engine → DirectX" looked identical; the
  `DisableGL` preference does not switch it (reset to 0).
* Steering garbage as the cause of the empty selector (the axis was never even read until the
  `SendPacket` abort was removed).

### State on disk (`D:\Games\NASCAR`), all reversible
* `Shell\NASCAR_Selection.am` (paths + SendPacket) — pristine OEM `NASCAR_Selection.am.oem`;
  `NASCAR_Selection.am.pathsok` = paths only.
* 16 more `.am` files with SendPacket removed — each `*.am.oem`.
* `OPERATOR_MENUS\XML\MachineInfoCol.xml`, `SysStatusCol.xml` — `*.xml.oem`.
* `Shell\bin\DongleStorageDevice.dll` = OEM again; the software one is
  `DongleStorageDevice.dll.gvrdongleemu`.
* `Shell\bin\GvrIO.dll` rebuilt from `src\GvrIOShim` (message-5 axis answer, links `user32`).
* AKLogShim COM registration removed (its exception handler cost frame rate); DLL still in
  `Shell\bin`.

### Next
1. ~~Track select is skipped (always Daytona)~~ — **solved later on 2026-10-04 (user-confirmed)**:
   `CabinetConfiguration_NAS1.TracksDisabled` must be **63** (a set bit = track available; the
   OEM row ships 0). Set by the user via Operator → Game Settings → Disable Tracks;
   `Tools\Build-NascarSqliteDb.py` now provisions it. The earlier "did NOT help" note was wrong —
   that test never reached the track screen.
2. ~~Operator → Game Settings → Disable Tracks freezes~~ — works with the value set; the single
   freeze (while the value was 0) was not reproduced.
3. "Game Crashed" = `Result_Status` 0 = unfinished race (ESCAPEREQUESTED 7 s into Daytona in
   `trace014.txt`); confirm whether that was the user's Esc/Q.
4. `MotionForce` dropdown in Machine Settings → Settings (motion plug-in absent).
5. Installer: fold in NASCAR_FINDINGS 11–12, and rebuild the shipped `game.db` with the updated
   `Build-NascarSqliteDb.py` (FreePlay = 1, TracksDisabled = 63).

### Display modes (2026-10-04, evening)
* "The race is a small window while the shell is fullscreen" = `nascar_settings.ini`
  `[Display]` defaults (1280×720, `Fullscreen=false`); the launcher appends
  `-windowed -width -height` to every race the shell starts. User now on 1920×1080.
* New `[Display] Borderless=true|false` in `NascarLaunch` (rebuilt): the race runs `-windowed`
  and the launcher strips the race window's frame and places it over its monitor (polls every
  250 ms while it waits on the shell; only `NASCAR_GVR.exe` is touched). Overrides `Fullscreen`;
  `Width/Height` 0 = screen size. Uses `QueryFullProcessImageNameA` (Vista+). Template ini in
  `src\NascarLaunch` documents it (default false); the user's install has it on.
* **The shell is always-on-top** (`WS_EX_TOPMOST` on the `AMPlayer` window — confirmed in the
  launcher log), which swallowed alt-tab. The launcher's 250 ms loop now demotes it to a normal
  window (`HWND_NOTOPMOST`, no focus change) — the NFSU-proven "clear it from outside" approach;
  the race window too unless in exclusive fullscreen (Direct3D owns the flag there). No
  re-assertion seen in the first 15 s.
* **Taskbar rule:** the shell hides `Shell_TrayWnd`. The launcher now re-shows it whenever neither
  `AMPlayer.exe` nor `NASCAR_GVR.exe` is the foreground window; while the game is in front it is
  left alone (the full-monitor game window keeps it behind anyway). The loop now runs in every
  display mode while the launcher waits (`WaitForExit=true`, the default).
* **Blurry race = 800×600 stretched.** The engine drops display modes larger than 1600
  (`0x507C5A`), so 1920×1080 was "invalid" and it fell back to 800×600. `GvrIOShim` now patches
  the limit to 4096 in memory (deployed to `Game\GvrIO.dll` and `Shell\bin\GvrIO.dll`; previous
  copies `GvrIO.dll.prev`). User-confirmed sharp; `trace020.txt`: "Using windowed screen
  resolution 1920 x 1080". NASCAR_FINDINGS.md section 13.
* Cut the 15 s startup "Please Wait" (`ioBoardDelay` 15.0 → 0.5 in `Shell.am` `Behavior95`;
  pure wait for a cabinet USB board — NASCAR_FINDINGS 13.4). Installer: apply with
  `Tools\Edit-AmScript.py`.
3. Installer: everything in NASCAR_FINDINGS 11–12 (`Patch-AmPaths`, `Patch-AmCalls`, the two
   operator XML cells, OEM `DongleStorageDevice`, plus the 11.x list).

## 2026-10-04 (night) — Xbox / PlayStation pads, NFSU layout

Request: the NFSU pad mappings on NASCAR, plus which buttons NASCAR adds or lacks.
Full write-up: `NASCAR_FINDINGS.md` §14; per-button table: `NASCAR_CONTROLS.md` section 4.

* **Finding that reshapes the controls docs:** the race never reads a DirectInput joystick (it
  creates only a keyboard), so the `.CTL` "controller 1" lines are dead. Wheel and pedals come in
  as GvrIO msg 5 axes (used whenever nonzero), buttons and the 4-position shifter as GvrIO
  `GvrIO.xml` names; `R C D G` are digital gas/brake/steer in the race. §5b corrected.
* **Built:** `GvrIOPad.cpp` in the GvrIO shim (both programs). Sticks/triggers answer msg 5 when
  no real pedals exist (now in the race too); buttons are OR-ed into GvrIO's own DirectInput
  keyboard state (IAT hook of `GvrIO_oem.dll`'s `DirectInput8Create` + private vtable copies), so
  they act as the cabinet keys and GvrIO raises the events. XInput + DS4 HID reader ported from
  GvrInputEmu, discovery on a background thread.
* **NASCAR-specific:** horn = the race's orphaned `NOS` event (new `<key char="h" name="NOS">` in
  `GvrIO.xml`, OEM kept as `.xml.oem`); sequential shifting over the 4-speed H-shifter; quit = the
  cabinet four-button abort (registry `FourButtonAbort`, not `-noabort`, which only disables the
  checkpoint timer).
* **Deployed:** `Game\GvrIO.dll`, `Shell\bin\GvrIO.dll` (previous: `GvrIO.dll.pre-pad`);
  `nascar_settings.ini` `[Controller]` / `[Frontend]` in the install and the template (backup
  `nascar_settings.ini.bak-*`). `GVRIOSHIM_LOG=1` set as a user variable for the first test —
  remove it afterwards.
* **Next:** in-game test with a real pad (none was connected while building): menus, race axes,
  every button, manual gears, hold-to-quit, and whether `NOS` really is the horn.
* **Test results (same night, PS4 DualShock 4 over Bluetooth) — user-confirmed working:** menus,
  driver / transmission / track select, steering + gas + brake, every button, manual shifting,
  horn, right-stick camera. Three fixes on the way (`NASCAR_FINDINGS.md` §14.3–14.4):
  1. *Buttons dead, keyboard cabinet keys too*: the first build swapped the DirectInput objects'
     vtable pointer; `dinput8` validates it, so GvrIO's `CreateDevice` failed (live memory:
     keyboard pointer 0). Now the shared vtable entries are redirected and filtered by `this`.
  2. *Transmission select*: it uses GvrIO's own `SelectionX`, stepped by a press of
     `VK_LEFT`/`VK_RIGHT` (scan codes `0x4B`/`0x4D`); the pad now presses those from the D-pad
     and from the stick over half-way.
  3. *Horn stuck on*: the cabinet `NOS` event restarts a looping sound and never stops it. The
     horn is now the engine's `.CTL` `Control - Horn`, bound to H (`(0, 35)`), held on the
     race's own keyboard by the pad; the `GvrIO.xml` `NOS` line is gone (OEM file restored).
  Added on request: the **right stick turns the Swingman camera** (numpad 4/6/8/2 on the race's
  keyboard). `GVRIOSHIM_LOG` removed again after the test.
* **Next:** fold into `Install-NASCAR-GVR-Portable.ps1` (shim already ships; add the `.CTL` horn
  binding and the ini sections — see the gvr-installer skill); try an Xbox pad and hold-to-quit.
* **Logging is an ini switch now** (user request): `nascar_settings.ini` `[Debug] Log=true|false`
  (default false; template and install) turns on the GvrIO shim's `gvrioshim.log`; the
  `GVRIOSHIM_LOG` environment variable still works as an override. Verified with a 32-bit test
  loader (log written with `true`, none with `false`). Shim redeployed to `Game\` and `Shell\bin\`.
* **Horn moved H → J** (2026-10-05): H is the engine's hard-coded HUD-cycle hotkey, so the pad's horn also flipped the HUD to the "Internal Pkts" overlay. `.CTL` `Control - Horn="(0, 36)"`, shim rebuilt; hard-coded keys listed in FINDINGS §14.4.
* **2026-10-05:** the J rebinding was lost because the race rewrites `GvrSinglePlayer.CTL` on exit with the bindings it loaded — edit it only with no race running. Removed the `GvrIO.xml` MotionDisable key (A on AZERTY popped the motion-seat HUD panel).
* **Horn done (2026-10-05, user-confirmed):** `.CTL` Horn is dead in this build; horn = GvrIO.xml NOS key `j` + shim `patch_horn_stop()` (stops the looping sample on release). Installer: add the NOS `j` line to `GvrIO.xml`, remove the MotionDisable `a` line, leave `.CTL` Horn unbound.
* **Registry self-repair in NascarLaunch (2026-10-05, user request):** every start checks all 25 machine values the installer writes (`required_values()`: identity keys must exist; folder paths, `GameRoot`, the three `gvr\Plus.1\Cabinet` paths and the ini-driven values must match this install) and rewrites what is missing or wrong through the existing elevated `--apply-settings` pass (one UAC prompt, only when something is actually wrong). Per-user values are fixed with no prompt: Anark client preferences and `UpTimeStart`/`UpTimeEnd` in the VirtualStore (empty = black-screen hang). **Shared-key hazard found:** `HKLM\SOFTWARE\gvr\Plus.1\Cabinet` holds both NFSU's `PatchDownloadPath` and NASCAR's `PlusSchemaPath` — one title's installer can repoint the other; the NASCAR launcher now re-asserts its own paths each start (NFSU's launcher should do the same for its values). Dry run on this PC: 25 checked, 0 to repair. Not yet exercised on a broken registry.
* **Private registry (2026-10-05, user-tested with a full race):** GvrIO shim answers `HKLM\SOFTWARE\GlobalVR` and `\gvr` from `<install>\nascar_registry.ini`; launcher writes settings there and preloads the shim into AMPlayer (suspended + APC). No admin, no NFSU collision. FINDINGS §15. Next: drop the installer's HKLM writes; same layer for NFSU.


## 2026-10-05 (late) - release: every hand fix is now an installer stage; fresh install verified

* **The gap.** `Install-NASCAR-GVR-Portable.ps1` (now `-4`) had none of the hand-applied
  section 11-13 fixes. They were found by diffing `D:\Games\NASCAR` against the disc payload,
  and every one is now an install stage:
  * `Apply-BytePatches` uses `Patches\nascar-bytepatches.txt`, made by
    `Tools\Make-BytePatches.py`. It holds same-size patches to `Dongle.dll`,
    `HerculesPlugIn.dll`, `NASCARPlugIn.dll`, `Shell.am`, `NASCAR_Attract/Selection.am` and 14
    operator scenes. Every patch is verified against the OEM bytes first, and the original is
    kept as `*.oem`.
  * `Edit-OperatorXml` handles the MAC and contact cells.
  * `Edit-GvrIoXml` adds J = NOS and removes A = MotionDisable.
  * `Disable-GammaSet`.
  * `msvcr71d` and `msvcp71d` are staged.
* **Disc input.** `-DiscPath`, or a prompt, plus extraction to `%TEMP%`. The directory names are
  repaired right after unshield; `-R` would rename unshield's own `File_Group`. The result is
  byte-identical to the dev payload (4903 files).
* **No machine-wide database variable.** The installer no longer sets `GVRSQLITE_DB_NAS1` for
  the whole machine. `NascarLaunch` points at its own install's `game.db`. The locally compiled
  provider is used directly, instead of being written over `Deploy\`.
* **The shipped `game.db` was stale** (`TracksDisabled 0`). It has been rebuilt, and it now also
  provisions `gOperatorFirstTimeInitilized = 1`. Without it, the first start ran the factory
  reset and the pedal-calibration screen (`NASCAR_FINDINGS.md` section 16).
* **Shim.** The pedal-range test now uses max - min < 40. The private registry gains a
  `FirstTimeStartup = 0` default.
* **Release tree:** `D:\NFSU_GVR\NASCAR_GIT`, with git initialised and **not committed**.
  * `README.md`: quick install, controls, settings, options, short findings.
  * `docs\findings\README.md`: bulleted findings with explanations, plus the full documents.
* **User-verified 2026-10-05.** A fresh install from the release folder into
  `D:\Games\NASCAR_Test` runs with free play, with no calibration screen and settings untouched.
