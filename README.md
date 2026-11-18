# NASCAR GVR

Play the GlobalVR arcade versions of **NASCAR Racing** (v1.1) and **NASCAR Team Racing** (2.0.2) on a normal Windows PC.

You get the real arcade menus, from the attract screen to the race and back. Xbox, PlayStation and Switch pads work, and so do most PC wheels. Your PC stays a normal PC: no shell replacement, no autostart, no reboot.

<p align="center">
  <a href="https://discord.gg/h7tBkSMbeB"><img src="https://img.shields.io/badge/Discord-join%20the%20community-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join the Discord community"></a>
</p>

## What you need

- Windows 10 or 11, 64-bit
- About 1 GB of free space per game
- One of these two game discs, as a disc, a mounted ISO or a copied folder:

| Game | Disc | ISO on the Internet Archive |
|---|---|---|
| NASCAR Racing | v1.1 game disc, part number 050-0136-01 | [Nascar_1.1_Game_050-0136-01](https://archive.org/details/isocd-Nascar_1.1_Game_050-0136-01.iso) |
| NASCAR Team Racing | 2.0.2 game disc, build 2.0.2.068 | [nascar-racing-2.0.2.068](https://archive.org/details/nascar-racing-2.0.2.068) |

Other discs, such as v1.5, do not work.

## Install

1. Download **`NASCAR-GVR-<version>-Setup.exe`** from [Releases](../../releases/latest).
2. Mount the game ISO: right-click it > **Mount**. Or insert the disc.
3. Double-click the Setup file. Click **Yes** when Windows asks for administrator rights.
4. If Windows says "Windows protected your PC", click **More info**, then **Run anyway**.
5. Choose the game. A mounted disc is already selected.
6. Pick a folder, for example `C:\Games`. The installer does the rest in a few minutes.
7. Start the game from its desktop shortcut.

To install both games, run Setup again and choose the other one.

If your antivirus blocks Setup, download the `.zip` from the same release. Unzip it and double-click `Install.bat`.

## Update

Download the newest Setup, run it and choose the game. It finds your install and asks "Update it now?".

No disc is needed. Your scores, settings, pedal calibration and race saves are kept. A copy goes to the `Backup` folder in the install folder first.

## Remove

Open Windows Settings > **Apps**. Choose **NASCAR Racing (GlobalVR arcade)** or **NASCAR Team Racing (GlobalVR arcade)**, then **Uninstall**.

Your scores, settings and calibration are copied to `Documents\NASCAR_GVR Backup` first.

## Controls

| | Keyboard | Xbox | PlayStation | Switch Pro |
|---|---|---|---|---|
| Steer | `D` / `G` | left stick | left stick | left stick |
| Gas / brake | `R` / `C` | RT / LT | R2 / L2 | ZR / ZL |
| Menus: select / back | `S` / `L` | A / B | Cross / Circle | B / A |
| Menus: up / down | `V` / `M` | D-pad | D-pad | D-pad |
| Race: start, or back on track | `S` | Menu | Options | + |
| Change view / look back | `V` / `L` | RB / LB | R1 / L1 | R / L |
| Turn the chase camera | numpad 4 6 8 2 | right stick | right stick | right stick |
| Horn | `J` | B | Circle | A |
| Gears (manual) | `T` `Y` `U` `I` = 1 to 4 | X / Y = up / down | Square / Triangle = up / down | Y / X = up / down |
| Music | `M` | D-pad right | D-pad right | D-pad right |
| Quit the race | hold `V` `L` `S` `M` | hold D-pad down | hold D-pad down | hold D-pad down |
| Operator menu | `O` | Menu, in the menus | Options, in the menus | −, in the menus |

Wheels, pedals, shifters and calibration: [docs/WHEELS.md](docs/WHEELS.md).

## Settings

All settings are in `nascar_settings.ini` in the install folder. Each one has a short note. Change a value, save, and start the game.

| Section | What it sets |
|---|---|
| `[Display]` | Screen size, window or fullscreen, race text size, graphics options. |
| `[Graphics]` | Race detail. |
| `[Cabinet]` | Music volume, the 19 extra drivers in NASCAR Racing. |
| `[Multiplayer]` | Your name and online play. See [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md). |
| `[Controller]`, `[Frontend]` | Pad buttons in the race and in the menus. |
| `[Wheel]` | Wheel, pedal and shifter overrides. |
| `[Debug]` | `Log = true` writes a `LOG` folder for bug reports. |

## Help

Something wrong? Start with [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md). It also explains how to report a problem.

Questions, or want to show your setup? [Join the Discord](https://discord.gg/h7tBkSMbeB).

## More

| Page | What it covers |
|---|---|
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Problems and fixes |
| [docs/WHEELS.md](docs/WHEELS.md) | Wheels, pedals, shifters, pads |
| [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md) | Online and network races |
| [docs/MENU_TEXTURES.md](docs/MENU_TEXTURES.md) | Changing the textures in the menus |
| [docs/MODS.md](docs/MODS.md) | Every change compared with the arcade |
| [docs/DEVELOPERS.md](docs/DEVELOPERS.md) | Building, repository layout, installer options |
| [docs/findings/](docs/findings/README.md) | Technical findings |

## Disclaimer

This is an unofficial fan project to preserve arcade software. NASCAR Racing and NASCAR Team Racing belong to their respective owners.
