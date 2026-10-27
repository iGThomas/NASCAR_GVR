# NASCAR GVR — the GlobalVR arcade NASCAR games, on a normal Windows PC

Installs the GlobalVR arcade cabinet versions of **NASCAR Racing** (v1.1) and **NASCAR Team
Racing** (2.0.2) as ordinary Windows games - either one, or both side by side. You get the real
arcade front end (attract screen → driver, transmission and track select →
race → back to the menus) in one folder of your choice. Xbox and PlayStation controllers work.
Your PC stays a normal PC: no shell replacement, no autostart, no reboot.

<p align="center">
  <a href="https://discord.gg/h7tBkSMbeB"><img src="https://img.shields.io/badge/Discord-join%20the%20community-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join the Discord community"></a>
</p>

## Quick install

**You need:**
- Windows 10 or 11, 64-bit (tested on Windows 11)
- the game disc - as a disc, a mounted ISO or a copied folder (the one with `data1.cab` on it):

| Game | Disc | ISO on the Internet Archive |
|---|---|---|
| **NASCAR Racing** | GlobalVR **v1.1 game disc**, part number **050-0136-01** | [Nascar_1.1_Game_050-0136-01](https://archive.org/details/isocd-Nascar_1.1_Game_050-0136-01.iso) |
| **NASCAR Team Racing** | GlobalVR **2.0.2** game disc (build 2.0.2.068) | [nascar-racing-2.0.2.068](https://archive.org/details/nascar-racing-2.0.2.068) |

  **Only these two discs are compatible.** The fixes are made for their files; other versions,
  such as the v1.5 disc, install but do not run (for example "entry point ... CardSwipeStop ...
  not found").
- about 1 GB of free space per game

**Steps:**

1. Download the latest **`NASCAR-GVR-<version>.zip`** from the
   [Releases](../../releases/latest) page and unzip it (right-click → **Extract All**).
2. Mount the game ISO (right-click it → **Mount**) or insert the disc.
3. Double-click **`Install.bat`** in the unzipped folder and click **Yes** when Windows asks
   for administrator rights. (If Windows asks "Do you want to run this file?", click **Run**.)
4. Choose the game: **NASCAR Racing** or **NASCAR Team Racing**. A mounted disc is recognised
   and already highlighted.
5. Pick where to install (for example `C:\Games` - a `NASCAR_GVR_1.1` or `NASCAR_GVR_2.0` folder
   is created inside it). The installer finds the mounted disc by itself, applies every fix and
   installs .NET 1.1 if it is missing. It takes a few minutes and tells you when it is done.
6. Start **NASCAR Racing** or **NASCAR Team Racing** from the desktop shortcut.

Both games can be installed side by side: run `Install.bat` again and choose the other one.

> **Antivirus warning?** Add the install folder as an exclusion (Windows Security → Virus & threat
> protection → Manage settings → Exclusions → Add a folder), and restore `GvrIO.dll` if it was
> quarantined (if `NascarLaunch.exe` vanished, unzip again and re-run `Install.bat`). This is a false alarm: to run an arcade game on a normal PC, our small DLLs step in
> between the game and Windows, and antivirus programs flag that kind of behaviour by default.

**Updating:** download the newest release, unzip it, run `Install.bat`, choose the game and pick
its existing install folder (e.g. `C:\Games\NASCAR_GVR_1.1`). It updates in place - no game disc needed, your settings
are kept (new settings are added), and it works even if you skipped releases. The installer only
ever touches a folder it installed itself (marked by `gvr-install.ini`); folders of other games,
other GVR titles or other NASCAR editions are never changed.
Your leaderboards, stats and operator settings (`GVR\GvrPlus\game.db`), your settings, pedal
calibration and race saves are always kept, and copied to `Backup\` in the game folder before
every install or update.

Screen size, fullscreen and the controller map are in `nascar_settings.ini` in the install
folder. Advanced: the installer still takes command-line options, e.g.
`Install.bat -Game 1.1 -Width 1920 -Height 1080` from an Administrator window (`-Game 1.1` or
`-Game 2.0` skips the game question).

### Controls

| | Keyboard | Xbox | PlayStation | Switch Pro |
|---|---|---|---|---|
| Steer | `D` / `G` | left stick | left stick | left stick |
| Gas / brake | `R` / `C` | RT / LT | R2 / L2 | ZR / ZL (on/off) |
| Menus: select / back | `S` / `L` | A / B | Cross / Circle | B / A |
| Menus: up / down | `V` / `M` | D-pad | D-pad | D-pad |
| Race: start (puts the car back on the track) | `S` | Menu | Options | + |
| Change view / look back | `V` / `L` | RB / LB | R1 / L1 | R / L |
| Turn the chase camera | numpad 4 6 8 2 | right stick | right stick | right stick |
| Horn | `J` | B | Circle | A |
| Gears (manual transmission) | `T` `Y` `U` `I` = gears 1–4 | X / Y = up / down | Square / Triangle = up / down | Y / X = up / down |
| Music | `M` | D-pad right | D-pad right | D-pad right |
| Quit the race | hold `V` `L` `S` `M` together | hold D-pad down | hold D-pad down | hold D-pad down |
| Operator menu | `O` | Menu (in the menus) | Options (in the menus) | − (in the menus) |

**Switch Pro Controller (new, not yet tested on real hardware):** USB or Bluetooth. Buttons follow the same
*positions* as the other pads, so A and B are swapped compared with Switch games. ZL / ZR are on/off; for
analog gas and brake set `Pedals = rightstick` under `[Controller]` in `nascar_settings.ini`
(right stick up = gas, down = brake). Steam can also present the pad as an Xbox controller.

**Steering wheels:** Logitech (Driving Force GT, G25, G27, G29, G920, G923), Thrustmaster and most
other wheels Windows lists as a driving device drive with their wheel and pedals, set up
automatically by model, with or without Logitech G HUB. Force feedback is the arcade's own
(centring, damping, grass and rumble-strip vibration, collision jolts). Gas and brake the wrong way
round? Set `SwapPedals = true` under `[Wheel]` in `nascar_settings.ini`. With `[Debug] Log=true`
the log lists every controller and names the wheel, its pedal layout and every button number it
reports.

**Direct-drive bases, separate pedals, shifters and button boxes:** just plug them in. Steering
comes from the wheel, gas and brake from the pedal box (found by name, or by the first pedal you
press), gears from a separate shifter (found by name or by a held gear; H-pattern or sequential,
recognised from how it is used; select Manual on the transmission screen), and a separate rim or
button box adds its buttons like the wheel's.

**Pedal calibration works like the arcade's:** the first time the game sees your pedals it shows
"Calibrate the accelerator and brake" - press each pedal all the way down once. The result is
saved (`nascar_calibration.ini`) and used by every race, so load-cell brakes and pedals limited in
their own software still reach 100 %. To calibrate again: operator menu (`O`) -> Control Test ->
hold **Start + Music**, then press each pedal fully (the same screen shows the steering wheel
following yours - a quick check that the wheel works). Calibration done in the wheel or pedal maker's
software (Fanatec, Moza, Thrustmaster ...) applies as usual. Force feedback works on bases Windows lists as a joystick too. With `[Debug] Log=true`
the log's `controls:` line says what each device became; a wrong guess is fixed with one line under
`[Wheel]` (`PedalDevice`, `ShifterDevice`, `ShifterMode`, `ButtonBox1`). Not yet tested on such
hardware.

Every control, and how to remap the pad: [docs/findings/NASCAR_CONTROLS.md](docs/findings/NASCAR_CONTROLS.md).

### Settings — `nascar_settings.ini`

In the install folder. The main ones:

- `[Display]` — `Width`, `Height`, `Fullscreen`, `Borderless`
- `[Controller]` / `[Frontend]` — the gamepad button map for the race and the menus
- `[Debug] Log=true` — writes diagnostics to a `LOG` folder in the install root: your PC config
  (CPU, motherboard, GPU + driver version), the launch timeline, a native crash filter for the
  shell and the race, and the database log. Reproduce the problem once, then zip the `LOG` folder.

### Installer options

| Option | What it does |
|---|---|
| `-InstallRoot D:\Games\NASCAR` | install folder (otherwise you are asked) |
| `-DiscPath E:\` | where the disc is (otherwise you are asked) |
| `-Width 1920 -Height 1080` | screen size |
| `-Fullscreen` | exclusive fullscreen instead of a window |
| `-Dxvk` | use DXVK (Direct3D 9 on Vulkan) if the race misbehaves on your GPU |
| `-NoShortcut` | no desktop / Start-menu shortcut |
| `-NoGui` | typed questions instead of the folder pickers |
| `-DryRun` | show what would happen, change nothing |
| `-Uninstall` | remove the install |

The install log is in `%TEMP%\NASCAR_GVR_Install\install.log`. If the race will not start, read
`Game\LOG\trace00N.txt` in the install folder first.

---

## What we found (short version)

- The cabinet runs **two programs**: an Anark front end (`AMPlayer.exe`) that launches the race
  (`NASCAR_GVR.exe`). Both had to work on a PC.
- The race had **three silent startup crashes**: a missing log folder, seven folders renamed by
  the disc extractor, and a deadlock in the cabinet I/O DLL. It also checks a **HASP dongle**,
  which is satisfied in memory without modifying the game.
- The front end checks the dongle, the I/O board, the coin monitor and the pedals, and **freezes
  silently** whenever a script calls cabinet hardware that is missing.
- The front end **changed the PC's time zone**, treated drive `D:` as the CD drive and ran the
  NVIDIA gamma tool. All of that is patched out.
- The **SQL Server database** is replaced by a single SQLite file, and the **registry** by an ini
  file in the install folder, so the install is portable and needs no other game files elsewhere.
- **Gamepads** are added at the cabinet I/O layer, so they drive the menus and the race just like
  the arcade wheel and buttons.

**All findings, one point at a time with explanations:** [docs/findings/](docs/findings/README.md)

---

## What is in this repository

| Folder | Contents |
|---|---|
| `Install.bat`, `Install-NASCAR.ps1` | the start: asks which game and runs its installer |
| `Install-NASCAR-GVR-Portable.ps1` | the NASCAR Racing (v1.1) installer |
| `Install-NASCAR-Team-Racing-Portable.ps1` | the NASCAR Team Racing (2.0.2) installer |
| `Patches\` | the byte patches the installers apply to the original files (checked before writing) |
| `Deploy\`, `Deploy-2.0.2\` | the SQLite database layer and the prepared `game.db`, per game |
| `src\` | source for our DLLs and the launcher (see below) |
| `Tools\` | the disc extractor (`unshield.exe`) and the research tools |
| `Dependencies\` | .NET 1.1 + SP1 installers, DXVK |
| `docs\findings\` | everything we learned |

**Source (`src\`):**
- `GvrIOShim` — the drop-in `GvrIO.dll`: startup fix, dongle check, screen-size limit, I/O board
  answers, gamepads, horn, and the private registry.
- `GvrPrivReg` — the private-registry library (answers registry calls from an ini file).
- `NascarLaunch` — `NascarLaunch.exe`: applies `nascar_settings.ini`, starts the front end, keeps
  the windows well-behaved.
- `GvrSqlite` — the SQLite provider the database layer uses.
- `GvrDongleEmu` — a software dongle back end (research; not installed).

The prebuilt binaries are included. To rebuild them you need Visual Studio (C++ desktop
workload): run `build.cmd` in each `src\` folder. To make a release download, run
`Tools\Make-Release.ps1 -Version <x.y.z>`: it packs only what the two installers need into
`dist\NASCAR-GVR-<x.y.z>.zip`, which is then uploaded to a GitHub Release.
`Tools\Make-BytePatches.py` / `Make-BytePatches-202.py` regenerate `Patches\nascar-bytepatches.txt`
/ `nascar-202-bytepatches.txt` from an original and a patched install.

## Community & help

Questions, bug reports, or want to show off your setup? **[Join the Discord](https://discord.gg/h7tBkSMbeB)** — https://discord.gg/h7tBkSMbeB

If the game will not start, bring these and someone can usually spot it quickly:
- `install.log` from `%TEMP%\NASCAR_GVR_Install\`
- after setting `[Debug] Log=true` in `nascar_settings.ini` and launching once, the `LOG` folder from the install root

## Disclaimer

This is an unofficial fan project for preserving arcade hardware software. NASCAR Racing and
NASCAR Team Racing are the property of their respective owners.
