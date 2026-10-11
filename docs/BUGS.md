# Known bugs

Open problems reported against the NASCAR GVR release. Newest first. Full investigation notes are
in `docs/findings/NASCAR_PLAN.md` / the dev tree's `NASCAR\Docs\NASCAR_PLAN.md` (dated entries).

| # | Bug | Affects | Status |
|---|---|---|---|
| 5 | Width/Height = 0 gives a low-resolution, stretched race on PCs with Windows display scaling > 100% | all v1.0.x | **Fixed in source** (1.0.9 launcher, 2026-10-12); workaround: real Width/Height in `[Display]` - user-confirmed |
| 4 | Re-install after an interrupted install fails: `[MISSING] GVR\GvrPlus\4\schema\NASCARcabinetXml.enc` | installer <= 4.10 | **Fixed in source** (installer 4.11, 2026-10-12) |
| 3 | Launcher's direct race mode always exits with -1: default `-series 2006NEXTEL` does not exist on the v1.1 disc | Everyone, v1.0.x | **Fixed in source** (2026-10-11, not yet released) |
| 2 | Shell hangs at startup, no window ever appears, on Windows 10 22H2 PCs | 3 reporters, v1.0.4+ | **Fixed for 1 reporter** (2026-10-12): clean .NET 1.1 reinstall + clean install (bug #4). Others: same advice, awaiting results |
| 1 | Taskbar flickers ("freaks out") while the shell runs, on PCs where something else keeps a taskbar hidden (taskbar tools, extra monitors) | NascarLaunch, v1.0.x | **Fixed in source** (2026-10-11, not yet released) |

---

## 5. Low-resolution, stretched race with Width/Height = 0 (display scaling)

**Reported:** 2026-10-12 - a player using direct race mode saw low-detail, stretched graphics with the
default `Width=0 / Height=0`; typing `Width=1920 / Height=1080` made it "a lot better".

**Cause:** NascarLaunch, AMPlayer and NASCAR_GVR.exe are not DPI-aware. With Windows display scaling
above 100% (125% is the laptop default) a DPI-unaware program is told a smaller screen
(1920x1080 @ 125% -> 1536x864): `GetSystemMetrics` gave the launcher 1536x864 for "0 = screen size",
the race rendered at that size, and Windows bitmap-stretched its window to the real screen. An earlier
reporter's log already showed `1536x864 borderless`.

**Fix (1.0.9 launcher):** `setup_dpi()` - the launcher declares itself per-monitor DPI-aware (real
screen size, real-pixel window placement) and sets `__COMPAT_LAYER=HighDpiAware` for its children, so the
shell and the race run DPI-aware without any exe change. Verified: the race's DPI awareness went from 0
(unaware) to 2 (per-monitor aware). `[Display] DpiAware=false` turns it off; the log records the
display scaling. Not reproducible on the 100%-scaled dev PC itself.

**Workaround (<= 1.0.8):** put the real resolution in `[Display]` Width / Height (user-confirmed).

---

## 4. Leftover disc extraction reused after an interrupted install

**Reported:** 2026-10-12 (the player who reinstalled .NET 1.1). Installer 4.10 log: `game disc found: F:\`
followed by no extraction at all, then `GVR helper tools source missing` and `GvrPlus payload source
missing`, and verification failed on `GVR\GvrPlus\4\schema\NASCARcabinetXml.enc`. Antivirus was off - the
old message wrongly suggested it.

**Cause:** the temporary extraction `%TEMP%\NASCAR_GVR_Extract` is deleted only at the end of a
SUCCESSFUL install. An interrupted one (here: the earlier .NET/regsvcs trouble) leaves it half-done, and
the next run checked only for `NASCAR\Game\NASCAR_GVR.exe` in it - present - so it skipped extracting and
copied from the incomplete folder.

**Fix (installer 4.11):** the disc branch deletes any leftover extraction before unpacking; after
unpacking, `Get-MissingPayloadParts` checks the game exe, the shell, `GVRPLUS\4\schema\NASCARcabinetXml.enc`
and `WINDOWS\system32` and stops with a clear message if one is missing; a reused `-PayloadRoot` gets the
same check; the SQLite stage fails instead of warning when `GVRPLUS` is absent; the verification failure
message no longer assumes antivirus.

**Workaround (<= 1.0.8):** delete `%TEMP%\NASCAR_GVR_Extract`, mount the disc, run `Install.bat` again.
**Confirmed 2026-10-12:** the player deleted the folder, re-ran the installer - no more errors.

---

## 3. Direct race mode always fails: the default series does not exist

**Found:** 2026-10-11, while investigating #2. Both reporters' "race launch" logs show it.

**Symptom:** the launcher's direct race (no shell) starts `NASCAR_GVR.exe -mode single -track
DAYTONA -series 2006NEXTEL -car 10 -laps 3 …`; a window appears and the game exits with
`4294967295` (-1) after ~3–4 s. The GvrIO shim logs `~cGvrIO on a never-initialised object`.

**Cause (reproduced on the dev machine, `Game\LOG\trace003.txt`):**
```
SERIES: 2006NEXTEL (2006 NEXTEL Cup)
game.cpp 3171: Warning : ** UNABLE TO LOAD SERIES '2006 NEXTEL Cup'
**FATAL** Game failed the ENTER state
*** BADSTUFF: Unhandled Exception! ***
```
The v1.1 disc only ships `2004 CTS`, `2004 NEXTEL Cup`, `2004 NNS` and `2005 NEXTEL Cup` .gdb
files. The same command without `-series` defaults to `2005NEXTEL` and races normally (verified,
`trace004.txt`).

**Where the bad default lives:** `src/NascarLaunch/NascarLaunch.cpp:213` (`ini_str("Race",
"Series", "2006NEXTEL")`), `src/NascarLaunch/nascar_settings.ini:27` (`Series=2006NEXTEL`), and
`Install-NASCAR-GVR-Portable.ps1:47` (`[string]$Series = "2006NEXTEL"`).

**Fix (2026-10-11, in source, unreleased):** default is now `2005NEXTEL` in all three places, and
the launcher checks that the series' `GameData\<year> <series>.gdb` exists before passing
`-series`; if not, it logs `series … is not on this disc` and leaves `-series` out, so installs
upgraded with the old `Series=2006NEXTEL` still kept in `nascar_settings.ini` work too. Verified
on the dev machine with `Series=2006NEXTEL`: no `-series` passed, game chose 2005NEXTEL, race
running at 20 s. Workaround on current releases: set `[Race] Series=2005NEXTEL`.

**Note:** shell mode is not affected — the shell passes its own arguments.

---

## 2. Shell hangs at startup with no window — Windows 10 22H2

**Reported:** 2026-10-11, by two users with very different hardware:

| | Reporter A (`C:\Games\NascarAC`) | Reporter B (`D:\NASCAR_GVR`) |
|---|---|---|
| OS | Windows 10 Home 22H2 (19045.6466) | Windows 10 Pro 22H2 (19045.6456) |
| CPU / RAM | Ryzen 7 5800X / 48 GB | Core 2 Quad Q8300 / 3.4 GB |
| GPU(s) | GTX 1070 (582.53) + RTX 2070 SUPER (newer branch) + **Meta Virtual Monitor** | GT 430 (391.35) + **Microsoft Basic Display Adapter** (likely the undriven onboard G31 graphics) |
| Desktop | 2560x1440 @ 180 Hz | 1366x768 @ 60 Hz |
| Shell mode | `fullscreen=false`, 1280x720 | fullscreen 1366x768 |

**Symptom:** `no visible window appeared within 10s`; AMPlayer keeps running until the user kills it.
(Both reporters' race failures are bug #3, not this one.)

**What the logs prove**
- Both `gvrioshim-AMPlayer-*.log` files stop right after the `mode-limit patch` line. On a working
  PC with the same build (`D:\Games\NASCAR_GVR\LOG`) the shim logs `pad: GvrIO_oem.dll
  DirectInput8Create redirected` + `watching heartbeat` 0.45–0.7 s after attach, then
  `Initialize(devType=4, hwnd=…)`.
- The shim's watchdog thread (created in `DllMain`) must log within ~5 s whether or not
  `GvrIO_oem.dll` is loaded. It never logs, so the main thread is most likely stuck holding the
  loader lock, i.e. stuck while loading a DLL.
- `ZeusIOPlugIn` (which pulls in `GvrIO_oem.dll`) is the first shell plug-in, and AMPlayer has no
  .NET import, so the hang is in **AMPlayer's own startup**: window, renderer (OpenGL by default;
  it statically imports `OPENGL32`, `GLU32`, `DDRAW`) and loading `Shell.am`. No GVR plug-in and
  no CLR have loaded yet.
- The `dongle patch: signature mismatch … NOT patching` / `mode-limit patch: bytes differ` lines are
  normal — identical on working PCs.

**What the two PCs share:** Windows 10 22H2 (all dev testing was on Windows 11). Both first had a
second, unusual display adapter (Meta's virtual monitor; Microsoft Basic Display Adapter), but
**Reporter A then removed the GTX 1070 and the Meta Virtual Monitor — single RTX 2070 SUPER, one
monitor — and the shell still hangs identically** (2026-10-11, 11:31 log). The multi-GPU /
virtual-monitor theory is ruled out for him.

**Suspects**
1. A graphics driver DLL (OpenGL ICD / DirectDraw) hanging while it loads. No longer tied to a
   second adapter (see above). Fits the loader-lock silence.
2. Our private-registry hooks (`GvrPrivReg`, 23 hooks, a critical section held around
   `Get/WritePrivateProfileString`) deadlocking when a driver reads the registry from inside its
   `DllMain`. The race uses the same hooks on Reporter B's PC and gets past them, so this is less
   likely but not ruled out.
3. Something specific to Windows 10's loader with the launcher's early-APC preload of `GvrIO.dll`.

**Asked of the reporters** (each one isolates a suspect)
1. From a Command Prompt in the install folder: `set GVR_NO_PRIVATE_REGISTRY=1` then
   `NascarLaunch.exe` — rules our registry hooks in or out.
2. Disable the extra adapter in Device Manager (Meta Virtual Monitor / Microsoft Basic Display
   Adapter), or disable the onboard graphics in the BIOS (Reporter B), and retry. *Reporter A did
   this: no change.*
3. While the shell hangs: Task Manager → Details → right-click `AMPlayer.exe` → **Create dump file**,
   and send the `.dmp`. The dump shows which DLL the main thread is stuck in — decisive.

**Repro option for us:** no Windows 10 VM exists here (only XP / Win7); one would let us test
suspects 1–3 directly.

**Result (2026-10-11): suspect 2 confirmed.** Both reporters ran `set GVR_NO_PRIVATE_REGISTRY=1`
+ the launcher and the shell started on both PCs. So the private-registry hooks cause the hang.

**Root cause (from the code):** `open_private()` in `src/GvrPrivReg/GvrPrivReg.cpp` took the
shared `g_lock` on **every** `RegOpenKey*` / `RegCreateKey*` in the process — Windows', every
driver's (often inside a `DllMain`, under the loader lock), and kernelbase's private-profile code
(IniFileMapping lookups, made while it holds its own ini lock) — *before* checking whether the key
was one of the game's. Meanwhile a thread answering a game key holds `g_lock` while it calls
`GetPrivateProfileStringA`, which needs kernelbase's ini lock. Two threads, two locks taken in
opposite orders: a deadlock. It depends on timing and on which drivers / system components read
the registry during AMPlayer's multi-threaded startup, which is why it hit these Windows 10 PCs and
never the Windows 11 dev machine. (The exact other thread is inferred, not captured — no dump.)

**Fix (in source, unreleased):** `open_private()` now decides "is this a game key?" without the
lock (`private_section()` uses only stack buffers) and takes `g_lock` only for the game's own keys,
so no foreign registry call ever waits on it. `NtQueryKey` is resolved once in `privreg_attach()`
instead of lazily (via `GetProcAddress`) inside a hook. The same `GvrPrivReg.cpp` is mirrored to
the NFSU repo (`GIT\src\GvrPrivReg`), which shares the code and the risk; NFSU's DLL is not rebuilt yet.

**Hotfix build:** `dist/hotfix-privreg/GvrIO.dll` = the **released** shim sources (HEAD) + the fixed
`GvrPrivReg.cpp` only — not the unreleased 2.0.2 shim work in the working tree. Install it as both
`Shell\bin\GvrIO.dll` and `Game\GvrIO.dll`. Verified on the dev machine with the private registry
active: shell reaches `Initialize` in 0.37 s and the IO-board checks; a direct race runs. 

**2026-10-11 (later): the lock fix did NOT cure it.** Reporter A installed the hotfix, launched
without `GVR_NO_PRIVATE_REGISTRY`, and the shell hung exactly as before (shim log again stops after
the mode-limit line). Caveat: that build printed no version line, so it can't be proven the hotfix
DLL was the one loaded. The lock change stays (it is still correct), but the cause is elsewhere in
the hook path.

**Diagnostic build** (`dist/hotfix-privreg/GvrIO.dll`, replaces the first hotfix; released shim
sources + GvrPrivReg changes only):
- logs `shim build: <date> <time> (private registry: lock only on game keys, RegTrace)` on every
  start, so a report shows which DLL ran;
- `[Debug] RegTrace=true` in `nascar_settings.ini` writes `LOG\regtrace-<exe>-<pid>.log`: every hooked registry call as a `>` entry line and a `<` return line with the calling module
  (K32GetMappedFileNameA), thread, key handle, subkey/value and result (`[private]` = a game key).
  Plain WriteFile on a handle opened once - no CRT file I/O, no lock. A hang in or because of a
  hook = a `>` line with no `<`; the module and key on it name the culprit.
- Dev machine: works, ~15,500 lines in 20 s of shell startup (dinput8, PerfCounter, NASCARPlugIn,
  AMPlayer, the Intel UMD, mscorwks, quartz, ...).

**2026-10-11 (night): not reproducible on a fresh Windows 10.** A fresh Windows 10 22H2 VirtualBox VM
(VBoxVGA, 3D acceleration off) installed with the 1.0.8 test build (installer 4.10) runs the shell with
the private registry active: hooks installed, plug-ins initialised in ~2.5 s, database reads OK, window
shown, DirectX renderer. So the hang is neither the installer nor Windows 10 as such - it is something
on the reporters' PCs. Prime suspect: their security software (Reporter A's antivirus flagged our DLL,
and a behaviour monitor freezing a process whose injected DLL patches registry code would look exactly
like this), then their specific drivers. (With 3D acceleration ON the VM showed a black window instead -
a VM graphics artefact, not this bug.)

**2026-10-12: Reporter A on v1.0.8 - still hangs.** Shim log shows `shim build: Oct 11 2026 19:35:55`
(the lock fix is in) and stops after the mode-limit line as before, so the lock was not the cause.
New: the launcher found **all 3 taskbar windows already hidden before launch** ("0 visible window(s)
recorded, 3 already hidden") - a taskbar/shell replacement is running (explains bug #1 on his PC; the
1.0.8 taskbar fix leaves them alone, no restore flood). Heavily modified system: antivirus that flags our
DLL + a taskbar tool, both of which typically inject into every process. Asked: one launch with real-time
protection paused / folder excluded, then `RegTrace=true` logs, and which antivirus + taskbar tool.

**2026-10-12: first RegTrace from Reporter A (v1.0.8, pid 7920) - the shell is NOT frozen at startup.**
The visible part (the paste was cut off) shows normal progress, every call returning:
`software\gvr\hercules\GvrShell` [private] OK; Anark prefs `DisableGL` present, `DisableDX7` /
`DisableSW` absent -> DirectX renderer; `nvd3dum.dll` (NVIDIA D3D UMD) loads and probes the adapter 5x;
then Anark rebuilds its font cache (`ANARK_FONTINFO0..286+` - a very large font collection). So the
"frozen in the first half-second" theory is wrong; the hang is later in AMPlayer's own startup.
Watchdog silence is therefore not proof of a held loader lock. Asked for the trace's last 150 lines: a
`>` without `<` = stuck in a registry call (names DLL + key); a normal end = hung outside the registry
(suspects: DirectX display-mode change on his 2560x1440 @ 180 Hz screen, the font scan).

**2026-10-12: Reporter C** (`C:\Games\NASCAR_GVR`, 1536x864 desktop = likely 125% scaling on 1920x1080,
one visible taskbar - no taskbar tool): v1.0.8, `install check: all files present`, same "no visible
window within 10s". Asked: .NET 1.1 file check + install.log line, `RegTrace` tail as a file, antivirus.
Note: NascarLaunch has no .NET 1.1 check yet (NFSU's GvrLaunch has `dotnet11_installed`); suggested
adding it so a missing runtime gives "run Install.bat" instead of a silent hang.

**2026-10-12: full RegTrace of a stuck run (`regtrace-AMPlayer-5188.log`, 1356 calls, 844 ms).**
Every call returns - no hook is stuck. Sequence: our GvrShell key, Anark prefs (DirectX renderer),
`nvd3dum.dll` adapter probes, font cache (326 entries), then **.NET 1.1 starts** (mscoree -> mscoreei ->
fusion -> mscorwks, CLR settings) and reads the user's number/currency formats from
`HKCU\Control Panel\International` (iDigits ... sCurrency). After `sCurrency` the process makes **no
further registry call** for the rest of its ~1 min run. A healthy run (dev PC) continues with fusion
assembly loads and then the plug-ins (`GvrIO_oem.dll`, HerculesPlugIn, ...). So the shell stops
**inside CLR 1.1 startup, before any game assembly or plug-in loads** - not in our hooks.
(The dev PC's format is `en-BE` / LCID 0x0C00, which CLR 1.1 does not know, so it never reads those
overrides; an en-US PC normally does - so the reads alone are not the fault.)
Next: the users' regional format + one launch with "English (United States)"; Task Manager CPU of the
hung AMPlayer (busy vs waiting) and any hidden .NET exception dialog; a RegTrace from the working Win10
VM to compare the same stretch.

**2026-10-12: a clean .NET 1.1 reinstall FIXED it for a player.** He uninstalled the .NET 1.1 our
installer had put on and installed `dotnetfx.exe` 1.1.4322.573 (English) - the game then worked. Our
bundle's `dotnetfx.exe` was 1.1.4322.579 **French** (both NASCAR and NFSU), but its `netfx.msi` has the
same ProductCode {CB2F7EDD-...}, ProductLanguage 1033 and **identical 243 files/versions** - only the
setup UI strings differ - so the language is not the fault; the player's existing 1.1 was damaged and
the reinstall repaired it. That matches the RegTrace (the hang is inside CLR 1.1 startup). Likely
damage source: the regsvcs.exe crash during the .NET MSI (its CLR dialog blocked/was cut short before
installer 4.10). Second suspect for the remaining hangs: `DbgJITDebugLaunchSetting = 16` (Windows
default, unknown to CLR 1.1) leaving a crashed shell waiting invisibly - a reinstall would not fix that.
Actions: English package now bundled (both titles); `Tools\Check-DotNet11.ps1` (read-only report:
runtime files + versions, GAC 1.0.5000.0 assemblies, ngen, policy, setup/uninstall keys,
DbgJITDebugLaunchSetting, regional format) to collect from a stuck PC BEFORE it is repaired, so the
installer/launcher can detect and repair that state automatically. Workaround for players: uninstall
"Microsoft .NET Framework 1.1", then run Install.bat (>= 1.0.8 / installer 4.10) again.

**2026-10-12: RESOLVED for that player - the game starts.** What fixed it, in order: uninstalled the
.NET 1.1 our installer had put on and installed the English `dotnetfx.exe`; deleted the leftover
`%TEMP%\NASCAR_GVR_Extract` (bug #4) and re-ran `Install.bat` (clean install, no errors); menu appears.
So at least this case of the "shell hangs, no window" bug was a **damaged .NET 1.1 install plus an
incomplete install**, not the hooks or the graphics. The same two steps are the advice for the other
reporters; 1.0.9 (installer 4.11) prevents both causes for new installs.

**Next for the reporters:** install 1.0.8 when released (lock fix, `shim build` stamp, `RegTrace`), then
one launch with the antivirus's real-time protection paused (or the install folder excluded). If that
starts the shell, it is the antivirus; if not, send `LOG\gvrioshim-AMPlayer-*.log` and
`LOG\regtrace-AMPlayer-*.log` with `[Debug] RegTrace=true`.

**Asked of Reporter A:** install the diagnostic DLL as both `Shell\bin\GvrIO.dll` and
`Game\GvrIO.dll`; set `[Debug]` `Log=true` and `RegTrace=true`; launch normally (no
`GVR_NO_PRIVATE_REGISTRY`); wait ~30 s; send `LOG\gvrioshim-AMPlayer-*.log` and
`LOG\regtrace-AMPlayer-*.log`.

---

## 1. Taskbar flickers while the shell runs (something else keeps a taskbar hidden)

**Reported:** 2026-10-11, Reporter A of #2 ("it also starts making my taskbar freak out").

**Cause — our launcher.** Nothing we install runs the OEM `HideTaskbar.exe`. `restore_taskbar()` in `src/NascarLaunch/NascarLaunch.cpp`
calls `ShowWindow(SW_SHOW)` on **every invisible** `Shell_TrayWnd` / `Shell_SecondaryTrayWnd`
every 250 ms while the shell is not the foreground window. On Reporter A's PC some taskbar windows
are hidden **by Windows itself** (most likely the one for the extra/virtual Meta monitor). The
launcher shows them and Explorer hides them again, four times a second. His log shows about 2
restores per tick (100 in ~13 s) and 2 more right after the race exits. It ran the whole time
because the shell never showed a window (#2).

**Supporting evidence:** Reporter B has the same shell hang but a single monitor, and his launcher
log has **no** taskbar-restore lines at all.

**Update (2026-10-11, 11:31 log — Reporter A with the GTX 1070 and Meta Virtual Monitor removed,
one GPU, one monitor):** still 5 restores within ~1 s during the shell, and 2 more after a direct
race that never ran the shell. So on his PC the **main** `Shell_TrayWnd` is kept hidden by something
outside the game that re-hides it within 250 ms every time — most likely taskbar-replacement
software (ExplorerPatcher, StartAllBack, …). The extra monitor was not the cause. On the dev machine
(Win11, one monitor) the shell really does hide the main taskbar once per session and the launcher
restores it once (`D:/Games/NASCAR/nascarlaunch.log`: 77 restores in 76 launches), so restoring is
still needed in general. No shell file names the taskbar (AMPlayer, plug-ins, UTF-16 .NET strings
all checked; only `HideTaskbar.exe`), so *how* the shell hides it is unknown.

**Fix (2026-10-11, in source, unreleased):** `snapshot_taskbar()` runs before each launch and
records only the taskbar windows (`Shell_TrayWnd`, `Shell_SecondaryTrayWnd`) that are **visible**
at that moment; ones already hidden are counted in the log and never touched. `restore_taskbar()`
re-shows a recorded window at most 3 times; if it is hidden again after that, it logs
`taskbar: the … keeps being hidden again (a taskbar tool?) - leaving it alone` and stops. Normal
case verified on the dev machine; the give-up path is code-reviewed, not live-tested. NFSU's
`GvrLaunch` has no taskbar code and is not affected.

**Ask Reporter A:** does he run ExplorerPatcher, StartAllBack or a similar taskbar tool?

**Workaround:** none needed beyond closing the shell; the taskbar settles once the launcher exits.
