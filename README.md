# NASCAR Team Racing — GlobalVR Arcade, on a normal Windows PC

Installs the GlobalVR arcade cabinet version of **NASCAR Team Racing** as an ordinary Windows
game. You get the real arcade front end (attract screen → driver, transmission and track select →
race → back to the menus) in one folder of your choice. Xbox and PlayStation controllers work.
Your PC stays a normal PC: no shell replacement, no autostart, no reboot.

## Quick install

**You need:**
- Windows 10 or 11, 64-bit (tested on Windows 11)
- the NASCAR Team Racing GlobalVR **v1.1 game disc** (part number **050-0136-01**, the one with
  `data1.cab` on it), as a disc, a mounted ISO or a copied folder. The ISO is on the Internet
  Archive: [archive.org/details/isocd-Nascar_1.1_Game_050-0136-01.iso](https://archive.org/details/isocd-Nascar_1.1_Game_050-0136-01.iso).
  **Only v1.1 is compatible.**
  The fixes are made for the v1.1 files; other versions, such as the v1.5 disc, install but do
  not run (for example "entry point ... CardSwipeStop ... not found").
- about 1 GB of free space

**Steps:**

1. Download the latest **`NASCAR-GVR-<version>.zip`** from the
   [Releases](../../releases/latest) page and unzip it (right-click → **Extract All**).
2. Mount the game ISO (right-click it → **Mount**) or insert the disc.
3. Double-click **`Install.bat`** in the unzipped folder and click **Yes** when Windows asks
   for administrator rights. (If Windows asks "Do you want to run this file?", click **Run**.)
4. Pick where to install (for example `C:\Games` — a `NASCAR_GVR` folder is created inside it).
   The installer finds the mounted disc by itself, applies every fix and installs .NET 1.1 if
   it is missing. It takes a few minutes and tells you when it is done.
5. Start **NASCAR Team Racing** from the desktop shortcut.

Screen size, fullscreen and the controller map are in `nascar_settings.ini` in the install
folder. Advanced: the installer still takes command-line options, e.g.
`Install.bat -Width 1920 -Height 1080` from an Administrator window.

### Controls

| | Keyboard | Xbox | PlayStation |
|---|---|---|---|
| Steer | `D` / `G` | left stick | left stick |
| Gas / brake | `R` / `C` | RT / LT | R2 / L2 |
| Menus: select / back | `S` / `L` | A / B | Cross / Circle |
| Menus: up / down | `V` / `M` | D-pad | D-pad |
| Race: start (puts the car back on the track) | `S` | Menu | Options |
| Change view / look back | `V` / `L` | RB / LB | R1 / L1 |
| Turn the chase camera | numpad 4 6 8 2 | right stick | right stick |
| Horn | `J` | B | Circle |
| Gears (manual transmission) | `T` `Y` `U` `I` = gears 1–4 | X / Y = up / down | Square / Triangle = up / down |
| Music | `M` | D-pad right | D-pad right |
| Quit the race | hold `V` `L` `S` `M` together | hold D-pad down | hold D-pad down |
| Operator menu | `O` | Menu (in the menus) | Options (in the menus) |

Every control, and how to remap the pad: [docs/findings/NASCAR_CONTROLS.md](docs/findings/NASCAR_CONTROLS.md).

### Settings — `nascar_settings.ini`

In the install folder. The main ones:

- `[Display]` — `Width`, `Height`, `Fullscreen`, `Borderless`
- `[Controller]` / `[Frontend]` — the gamepad button map for the race and the menus
- `[Debug] Log=true` — writes a trace when something goes wrong

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
| `Install-NASCAR-GVR-Portable.ps1` | the installer |
| `Patches\` | the byte patches the installer applies to the original files (checked before writing) |
| `Deploy\` | the SQLite database layer and the prepared `game.db` |
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
`Tools\Make-Release.ps1 -Version <x.y>`: it packs only what the installer needs into
`dist\NASCAR-GVR-<x.y>.zip`, which is then uploaded to a GitHub Release. `Tools\Make-BytePatches.py` regenerates
`Patches\nascar-bytepatches.txt` from an original and a patched install.

## Disclaimer

This is an unofficial fan project for preserving arcade hardware software. NASCAR Team Racing is
the property of its respective owners.
