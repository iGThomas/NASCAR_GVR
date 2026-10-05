# NASCAR Team Racing (GlobalVR) — findings

Everything we learned getting the arcade cabinet's software to run as an ordinary Windows game.
Each point says what we found and why it matters. The section numbers (§) point into
[NASCAR_FINDINGS.md](NASCAR_FINDINGS.md), which has the addresses, byte offsets and evidence.

Other documents in this folder:

| Document | What it covers |
|---|---|
| [NASCAR_FINDINGS.md](NASCAR_FINDINGS.md) | The full technical findings (the source for everything below) |
| [NASCAR_CONTROLS.md](NASCAR_CONTROLS.md) | Every control: keyboard, cabinet buttons, Xbox / PlayStation pads |
| [NASCAR_REGISTRY.md](NASCAR_REGISTRY.md) | Every registry key the game and the menus read |
| [NASCAR_SQLITE.md](NASCAR_SQLITE.md) | How the SQL Server database was replaced with SQLite |
| [NASCAR_INSTALL_ANALYSIS.md](NASCAR_INSTALL_ANALYSIS.md) | What the original cabinet installer does, and what we avoid |
| [NASCAR_AUTOSTART_AND_INSTALL_REGISTRY.md](NASCAR_AUTOSTART_AND_INSTALL_REGISTRY.md) | How the cabinet takes over Windows at boot (never reproduced here) |
| [NASCAR_PLAN.md](NASCAR_PLAN.md) | The day-by-day engineering log |

---

## The two programs

- **The game is two programs.** `AMPlayer.exe` (the "shell") shows the attract screen, driver /
  transmission / track select and the operator menus. It starts `NASCAR_GVR.exe` (the race) and
  stays open behind it; the race exits back to the menus. (§9)
- **`AMPlayer.exe` is GlobalVR's own shell, not a stock Anark player.** Its build paths and
  window titles say "GvrShell", so everything the cabinet ran is on the disc — no missing
  program. (§9.1)
- **The menus are Anark scenes (`.am`) with JavaScript inside.** The scripts are zlib-compressed
  inside each `.am`; `Tools\Extract-AmScripts.py` unpacks them. Reading the scripts was much
  faster than reverse-engineering the binaries. (§11.1)
- **A scene script that calls a missing function stops silently.** No error, no log — the screen
  just freezes on whatever it showed. Most "it hangs on Please Wait..." problems were this.
  `Tools\Find-AmMissingCalls.py` lists every such call. (§12.1)

## Getting the race to start

- **The game keeps its own log, but the disc ships no `LOG` folder.** Without the folder the game
  writes nothing and seems to fail silently. With it, `Game\LOG\trace00N.txt` records the command
  line, the dongle result, every startup step and every error. Read it first. (§8.1)
- **Unpacking the disc renames seven folders, and that crashes the game.** The extractor turns
  spaces in folder names into underscores (`Basic Settings` → `Basic_Settings`). The game has
  those names built in; when one is missing it reads a null pointer and quits with no message.
  The installer puts the names back. (§8.2)
- **The HASP dongle check is built into the race.** There is no DLL to swap. The GvrIO shim
  rewrites the check in memory before the game's `main()` runs, so the executable is never
  modified on disk. (§8.3)
- **The race needs a track on its command line.** `-track Daytona -startPos 43` works. The
  game's argument parser ignores quotes, so names with spaces cannot be passed. (§8.4)
- **`GvrIO.dll` hangs the race at startup.** The race creates and destroys GvrIO without
  starting it, and the original DLL then waits forever. Our drop-in `GvrIO.dll` forwards to the
  original and fixes only that. (installer, `src\GvrIOShim`)
- **The race refuses screen sizes above 1600.** A hard-coded limit drops every larger display
  mode, so 1920×1080 fell back to a stretched 800×600. The shim raises the limit in memory. (§13.1)

## Getting the menus to run

- **The menus check for a dongle too, in a different place.** The cabinet plug-in `Dongle.dll`
  reads the key; it is patched to report a valid "NASCAR 1.0" key. Version "1.5" is rejected as
  the wrong game version. (§11.2)
- **The operator plug-in silently failed to load.** `NASCARPlugIn.dll` needs the *debug* C++
  runtime (`msvcp71d.dll`, `msvcr71d.dll`), which is on the disc but was never installed. Without
  it the menus sit on "Please Wait..." forever. (§11.3)
- **An empty "up time" value froze the menus on a black screen.** The startup script parses two
  dates that a new install does not have, and fails every frame. They are now always filled in.
  (§11.4)
- **The cabinet waits for hardware that is not there.** A coin-monitor wait made the menus run at
  about 4 fps, and a 15-second wait for the I/O board slowed startup. Both waits are patched out;
  the I/O board and pedal checks are answered by the shim. (§11.6, §11.7, §13.4)
- **Car select needed three fixes.** The scenes loaded car skins from `C:\NASCAR\...` (now
  relative paths); a call to the missing cabinet-linking board froze driver select (removed in 17
  scenes); and a software dongle made the cabinet "international", hiding four drivers. (§12.3)
- **Track select was skipped because no tracks were enabled.** The setting called
  `TracksDisabled` really means "tracks *enabled*", and the original database ships 0. The
  database now enables all six tracks. (§12.6)
- **The first start ran a cabinet "factory setup".** The operator plug-in sees a database flag
  still at 0 and resets every setting to factory defaults: free play off and the coin-op options
  switched on. It also asks for pedal calibration and sets the PC's time zone. The shipped database
  marks that setup as already done. (§16)
- **The menus run on a SQLite file instead of SQL Server.** The original needs MSDE / SQL Server
  2000. The database layer is redirected to a single `game.db` file. ([NASCAR_SQLITE.md](NASCAR_SQLITE.md))
- **The menus need the old .NET 1.1, and break on newer .NET.** A small `.config` file pins them
  to 1.1. (installer)

## Keeping it a normal PC

- **The operator plug-in changes the Windows time zone** to the cabinet's ("Turkey Standard
  Time"). It is patched so it only reads the time zone. (§11.5)
- **It treats drive `D:` as the CD drive**, so any PC with a `D:` drive got "There is a CD in the
  drive". Patched. (§11.5)
- **It runs the NVIDIA gamma tool (`GammaSet.exe`)** on every start, which pops up an error or
  changes your screen colours. Disabled. (§11.5)
- **The menus hide the taskbar and force "always on top".** The launcher puts the taskbar back
  and makes the windows ordinary, so alt-tab works. (§13.3)
- **No Windows registry is used.** The game's keys are answered from `nascar_registry.ini` in the
  install folder, with every folder worked out from where the install is. So no administrator
  rights are needed for the game's settings, a moved install keeps working, and it cannot clash
  with another GlobalVR game installed on the same PC (they share one registry key). (§15)
- **Nothing of the cabinet takeover is installed.** The original installer replaces the
  Explorer shell, hides the desktop, autostarts at boot and reboots. None of that is reproduced.
  ([NASCAR_INSTALL_ANALYSIS.md](NASCAR_INSTALL_ANALYSIS.md))

## Controls

- **The race only listens to the cabinet I/O layer (GvrIO), not to joysticks.** The controller
  lines in the race's `.CTL` file are never read: this build opens no joystick. (§14.1)
- **Xbox and PlayStation pads are added inside the GvrIO shim**, so they work in the menus and
  the race exactly like the cabinet wheel and buttons. Steering and pedals are analog, and every
  button can be remapped in `nascar_settings.ini`. (§14, [NASCAR_CONTROLS.md](NASCAR_CONTROLS.md))
- **The horn is the NOS button.** `H` is the engine's HUD hotkey (it cycles a debug overlay), and
  the `.CTL` horn makes no sound. The horn is mapped to `J`, and the shim stops the looping sound
  when the button is released. (§14)
- **`A` opened the motion-seat panel mid-race** and was removed from the key map. (§14)

## Still open

- Some operator screens that a home install does not need still call cabinet-only functions
  (golf leftovers, smart-card purchase screens, the motion test). (§12.1)
- The "Game Crashed" message after an aborted race is the shell's wording for any unfinished
  race; it is not a crash. (§12.6)
