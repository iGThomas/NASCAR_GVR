# For developers

How the project is built, what is in the repository, and the installer's command-line options. Players do not need this page.

## Findings in short

- The cabinet runs two programs. The Anark menus (`AMPlayer.exe`) start the race (`NASCAR_GVR.exe`). Both had to work on a PC.
- The race had three silent start-up crashes. A log folder was missing. The disc extractor renamed seven folders. The cabinet I/O library deadlocked.
- The race checks a HASP dongle. The check is answered in memory, without changing the game files.
- The menus check the dongle, the I/O board, the coin monitor and the pedals. They freeze silently when a script calls missing cabinet hardware.
- The menus changed the PC's time zone, treated drive `D:` as the CD drive and ran the NVIDIA gamma tool. All of that is patched out.
- The SQL Server database is replaced by one SQLite file. The registry is replaced by an ini file in the install folder. So the install is portable.
- Gamepads are added at the cabinet I/O layer. They drive the menus and the race like the arcade wheel and buttons.

Every finding with its explanation: [findings/](findings/README.md). Every change compared with the arcade: [MODS.md](MODS.md).

## Repository layout

| Folder | Contents |
|---|---|
| `Install.bat`, `Install-NASCAR.ps1` | The start. Asks which game and runs its installer. |
| `Install-NASCAR-GVR-Portable.ps1` | The NASCAR Racing (v1.1) installer. |
| `Install-NASCAR-Team-Racing-Portable.ps1` | The NASCAR Team Racing (2.0.2) installer. |
| `Patches\` | Byte patches for the original files. Each one is checked before writing. |
| `Deploy\`, `Deploy-2.0.2\` | The SQLite database layer and the prepared `game.db`, per game. |
| `Deploy\DDrawCompat\` | DDrawCompat v0.7.2 (0BSD), a Direct3D 7 helper for the menus. |
| `src\` | Source of the DLLs and the launcher. |
| `Tools\` | The disc extractor (`unshield.exe`) and research tools. |
| `Dependencies\` | Microsoft's .NET 1.1 and SP1 packages, unpacked into the game folder at install time. DXVK. |
| `relay\` | The relay server for internet multiplayer. One Python file, Docker optional. |
| `docs\findings\` | Technical findings. |

## Source

| Folder | Builds | Purpose |
|---|---|---|
| `src\GvrIOShim` | `GvrIO.dll` | Start-up fixes, dongle answer, screen-size limit, I/O board answers, gamepads, wheels and the private registry. |
| `src\GvrPrivReg` | library | Answers registry calls from an ini file. |
| `src\NascarLaunch` | `NascarLaunch.exe` | Applies `nascar_settings.ini` and starts the menus. Keeps the windows well-behaved. `NascarNet.cpp` handles multiplayer. |
| `src\GvrSqlite` | `GvrSqlite.dll` | The SQLite provider for the database layer. |
| `src\GvrLinking` | `GvrLinking.dll` | The menus' cabinet link over the internet relay. The original used IPX. Used only with `[Multiplayer] Mode = lobby`. |
| `src\GvrGlu` | `glu32.dll` | Makes the menus load `GvrIO.dll` at start. Every glu function passes on to Windows' own `glu32.dll`. |
| `src\MenuTextures` | `GvrMenuTextures.exe` | The menu texture editor, and the `apply` step the launcher runs at start. |
| `src\DriverArt` | `GvrDriverArt.exe` | Builds the pictures of the 19 extra drivers on the player's PC. |
| `src\GvrDongleEmu` | | A software dongle back end. Research only, not installed. |
| `src\Setup` | `Setup.exe` | The one-file download. Unpacks the package to a temporary folder, runs `Install-NASCAR.ps1`, then removes the folder. |

## Textures in the menu scenes

Each texture in an `.am` scene has a 64-byte header. Its first 4 bytes hold the texture's length plus 64. After the header comes one of these:

- one PNG
- one JPEG
- a colour JPEG directly followed by a grey JPEG, which is its transparency mask

The textures are stored upside down. PNG textures also have red and blue swapped. A replacement keeps the exact length of each part, so nothing else in the file moves. The spare bytes go into a private `gvRp` chunk in a PNG, or a comment segment in a JPEG. Replacements live in `Shell\Mods\<scene>\`. The scene without them is kept as `<scene>.am.nomods`. Code: `src\MenuTextures`.

## Building

The prebuilt files are included. To rebuild them you need Visual Studio with the C++ desktop workload. Run `build.cmd` in each `src\` folder.

To make a release, run `Tools\Make-Release.ps1 -Version <x.y.z>`. It writes two files to `dist\`:

- `NASCAR-GVR-<x.y.z>.zip`, with only what the installers need
- `NASCAR-GVR-<x.y.z>-Setup.exe`, the same package in one file

`Tools\Make-BytePatches.py` and `Make-BytePatches-202.py` rebuild the patch lists from an original and a patched install.

## Installer options

Players do not need these. They are for testing and support. Run them from an Administrator window.

| Option | What it does |
|---|---|
| `-Game 1.1` or `-Game 2.0` | Skips the game question. |
| `-InstallRoot D:\Games\NASCAR` | The install folder. Otherwise the installer asks. |
| `-DiscPath E:\` | Where the disc is. Otherwise the installer asks. |
| `-Width 1920 -Height 1080` | The screen size. |
| `-Fullscreen` | Exclusive fullscreen instead of a window. |
| `-Dxvk` | Uses DXVK (Direct3D 9 on Vulkan) if the race misbehaves on a graphics card. |
| `-NoShortcut` | No desktop or Start menu shortcut. |
| `-NoGui` | Typed questions instead of folder pickers. |
| `-DryRun` | Shows what would happen and changes nothing. |
| `-RepairDotNet` | Unpacks the game's private .NET 1.1 again. |
| `-Uninstall` | Removes the install. |

Example: `Install.bat -Game 1.1 -Width 1920 -Height 1080`

The install log is in `%TEMP%\NASCAR_GVR_Install\install.log`. If the race does not start, read `Game\LOG\trace00N.txt` in the install folder first.
