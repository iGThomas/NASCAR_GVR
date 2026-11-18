# Changes compared with the original arcade

This page lists what the packages change on top of the original cabinet software.

- **Mods** change gameplay or content on purpose.
- **Fixes** are what a PC needs to run the game at all.

Where a change has a setting, it is in `nascar_settings.ini`, and the original behaviour is one setting away. Program files on disk stay original unless the table says otherwise. Changes marked *in memory* are made by the `GvrIO.dll` shim while the game runs.

## Both games

### Mods

| Change | Original arcade | Setting |
|---|---|---|
| Online multiplayer: races over the internet through a relay server, with no router setup, or on the same network. Straight into a race, in both games. See `docs/MULTIPLAYER.md`. | Linked cabinets on one arcade network only. The menus' link used IPX, which Windows no longer has. | `[Multiplayer] Mode = host / join`, `Relay`, `Room`, `Players`, `HostAddress`. `Mode = off` is the original. |
| Menu texture editor: `Tools\GvrMenuTextures.exe` shows every menu texture and replaces it with your own. Replacements are kept in `Shell\Mods` and survive updates. See [MENU_TEXTURES.md](MENU_TEXTURES.md). | Textures could only be changed by editing the scene files by hand, and an update undid it. | None |
| Race graphics options: `AITextureSize`, `EnvironmentMap`, `CompressedTextures`, `DynamicLighting` and `MaxVisibleCars` (5 to 48). They are written into the race's player profile at every start. | Fixed in the player profile | `[Graphics]`. The defaults are the arcade's values, except `AITextureSize=0`. The disc's paints are 512 anyway, and 0 also stops custom paint crashing on computer cars. |
| Most detailed car models at every distance. The car body and crash debris always use their best model. Tyres, driver and shadows keep their steps. The lists are made from the disc's own model lists at install time. | Cars switch to simpler 3D models with distance | `[Graphics] MaxCarDetail=true` |
| Per-car rims: the race reads `<paint folder>\<paint name>rims.bmp` (256 x 256) after the shared `wcrims.bmp`. Blue rims for the #2 car come with the game: `Dodge\2\2-01\2-01rims.bmp`. | Every car used the same grey rims | None |
| Race text (place, lap, speed, lap times) grows with the screen. The launcher sizes the race fonts at every start. The original `HUD.ini` is kept as `HUD.ini.oem`. | Fonts of a fixed size, so the text got smaller on bigger screens | `[Display] HudText` |

### Fixes

| Problem on a PC | Fix | Setting or switch |
|---|---|---|
| Menus never showed a window on some PCs, seen on Windows 10 22H2 and Windows 10 IoT LTSC 24H2. A menus library starts .NET while Windows' DLL-loading lock is held. .NET's first COM call then waited forever. | Safe start: before the menus load anything, the GvrIO shim prepares COM with one round trip. It then starts the install's .NET itself, on threads of its own. The menus' DLL loads wait for it, at most 30 s. *In memory.* | `[Launcher] SafeStart=false` turns it off |
| Menus never started, or hung with no window. The .NET 1.1 installed into Windows was damaged or could not be installed. Its SP1 package needs an older .NET to unpack, which a clean Windows 10 or 11 does not have. | The installer unpacks .NET 1.1 SP1 into the game folder (`Runtime\`) and copies its libraries next to the front end. The launcher starts the menus on that copy. Nothing of .NET 1.1 is installed into Windows. A copy already there is left alone and not used. | `Install.bat -RepairDotNet` unpacks the game's copy again |
| On laptops with two graphics chips, the game could run on the slower built-in chip. There, the menus' graphics tests can fail. | The launcher asks Windows to use the faster chip for the game. A choice made in Windows' Graphics settings is kept. | `[Display] Gpu` |
| Menus stuck on "Please Wait" with no explanation when the PC offers neither Direct3D 7 nor OpenGL | The launcher says why and what to do: no graphics driver, a remote or virtual screen, or the monitor on the motherboard's port. The game starts anyway. | None |
| Menus' old Direct3D 7 broken on some newer graphics drivers | The game comes with DDrawCompat, an open-source helper, switched off. The launcher turns it on for the menus only when the PC fails their graphics tests. | `[Display] DDrawCompat` |
| Some antivirus programs blocked the way the launcher put its fixes into the menus | The menus now load the fixes by themselves, through `Shell\bin\glu32.dll`. If that fails, the launcher uses the older way and logs it. | `[Launcher] Preload` |
| Windows' Smart App Control blocked one of the game's unsigned files, and the game failed with no message | After each session the launcher checks Windows' block log for the game's files. It logs them and names the blocked file in a message. | None |
| The game installed in a folder it cannot work from did not start, with only "settings file not found". Examples: letters outside the Windows language, a very long path, Program Files, OneDrive, Desktop, Documents. | The installer refuses such a folder and suggests C:\Games. The launcher explains the problem instead. | None |
| DirectX device list included detached and non-display devices, so an inactive virtual monitor could be picked | Only attached displays are offered. Every device, and the one the renderer opens, is written to the log. *In memory.* | `GVRIOSHIM_NO_DD_FIX=1` turns it off |
| PC wheel that does not report itself as a wheel steered in the menus but not in the race. Example: direct-drive bases such as MOZA. Exclusive access for force feedback was refused, and the wheel was then not read at all. | The wheel is read shared when exclusive access is refused. Steering is kept, without force feedback from the shim in that program. | None |
| Wheel-and-pedals bundles with "Pedals" in their name were taken for a pedal box: pedals worked, steering did not. Example: MOZA R3, "R3 Racing Wheel and Pedals". | A device with force feedback, or "wheel" in its name, is never a pedal box. MOZA R3 pedal axes are preset: gas z, brake rz. A `[Wheel] Device` name that matches nothing falls back to automatic choice. `Steering = auto` is accepted and means x. | `[Wheel]` |
| A generic USB H-shifter acted as extra buttons until it was recognised: gears changed the camera or shifted up and down. Each race had to recognise it again. | It is recognised from two held gears, even when it reports unused axes or a held mode switch. It is remembered in `nascar_calibration.ini` `[Shifter]`. | `[Wheel] ShifterDevice = none` turns shifters off |
| Transmission screen ignored a PC wheel when the first menu message came before the wheel was found | Menu selection moves to the shim as soon as a PC wheel or pedal box is found | None |
| Front end start-up could wait for a slow controller scan. Some USB devices answer slowly. | The scan runs in the background. Start-up waits for it at most 3 s. | None |
| Front end crashed while closing, with exit code 0xC0000005 in the log | The shim's I/O-board watchdog stops when `GvrIO_oem.dll` is unloaded | None |
| Database password of the original cabinet written to the log | Password masked in `gvrsqlite.log` | None |
| Nightly restart prompt restarted the PC | `NASCARPlugIn.dll` patched in the file: the restart check always answers "not yet". The shim also blocks `shutdown.exe` and the Windows restart calls *in memory*. | None |

## NASCAR Racing 1.1

Gamepad and PC wheel support, the horn stopping on release, the private registry and the SQLite database are described in `docs/findings/NASCAR_FINDINGS.md`.

### Mods

| Change | Original arcade | Setting |
|---|---|---|
| 31 playable drivers. The 19 race cars on the disc without a driver select slot are added after the original 12: Kevin Harvick, Mark Martin, Joe Nemechek, Bobby Labonte, Dale Jarrett and more. Each has its own car, name plate, number and team lines. | 12 drivers. The other 19 cars race only as AI opponents. | `[Cabinet] ExtraDrivers = true`, and `false` for the original 12. Scene `NASCAR_Selection.am` patched, original kept as `*.oem`. The driver list is in `nascar_registry.ini` (`ExtraDriverN`). |
| Pictures of the added drivers are built on the player's PC from the disc's own files. Name plates come from the disc's letters and numbers from each car's own paint. Leaderboard badges and menu car pictures come from the race files. Portraits are placeholders showing the name and number. When NASCAR Team Racing is installed on the same PC, its portrait is used for drivers both games share. | None | Built by `Tools\GvrDriverArt.exe` at install |
| Player-made driver pictures: replace any file in `Shell\Textures\Faces` (256x256 portrait), `Names` (512x64 name plate) or `Numbers` (128x64 number). The file name is the car number and name, as already there. The launcher puts changed pictures into the menus at the next start. Updates and backups keep them. | None | `Shell\Textures\Faces\README.txt` |
| New number plates for all 19 extra drivers and a Kevin Harvick portrait come with the game (`Shell\Textures\Community`). They replace a picture only while it is still the automatic one, so a player's own pictures stay. When Team Racing is installed, its portraits of shared drivers are used for placeholder faces. | Automatic number plates made from the disc, placeholder portraits | None |
| Extra drivers' card colours: the checkered background behind each number can have its own colour per extra driver. It is the `card colour R,G,B` column in `Tools\extra-drivers.txt`, from the package. | The 19 extra drivers borrow one of the 12 original card colours | Package data |
| Leaderboard (high-score screens) shows the car badge of every car, with #01 and #1 kept apart | Badge only for the original 12 cars | Scene `NASCAR_Attract.am` patched |
| Arcade lobby over the internet: the menus' cabinet link works again through a relay server. "Join now! Play head-to-head", driver choice, Lobby with countdown, race together, up to 10 players. Lobby cards show each player's own driver, extra drivers included. Player names show on the Lobby cards and on the cars in the race. | Linked cabinets on one arcade network, over IPX, which Windows no longer has. "Driver 1", "Driver 2". | `[Multiplayer] Mode = lobby`, `Relay`, `Room`, `PlayerName`. `Mode = off` is the original. |

### Fixes

| Problem on a PC | Fix | Setting or switch |
|---|---|---|
| Attract never loaded on Windows set to a day/month date format, for example Dutch, German or Japanese | The shim runs the front end with US region settings. *In memory.* | `GVRIOSHIM_NO_LOCALE_FIX=1` turns it off |
| Race picture blurry on screens wider than 4096 pixels, for example 5120x1440 | The shim accepts display modes up to 16384 pixels. *In memory.* | None |
| PC wheel with pedals, a gear shifter, a separate rim or button boxes on their own USB cables was ignored. Only one controller was read. | All are read together and set up automatically | Overrides under `[Wheel]` |
| Menus stuck on a frozen "Please Wait", with music playing, on PCs where the game's own Direct3D 7 test fails. It then greys out Direct3D and OpenGL and draws in software. | The launcher repeats that test before every start. If it passes, the menus use Direct3D 7 as on the cabinet. If it fails, they use OpenGL. | `[Display] Renderer = auto`, or `opengl` / `directx` to force one |
| With NASCAR Team Racing installed on the same PC, the front end loaded Team Racing's AKLog2 stand-in | The shim answers "not registered" for AKLog2 in this front end, as on a PC without it. *In memory.* | None |
| Menus stuck on the "game crashed" screen after a race was closed with Alt+F4 or crashed. It waited for a dongle answer that never comes after a race. | That screen returns to the attract after about 4 s. `Launch_Game_Error.am` patched. | None |

## NASCAR Team Racing 2.0.2

### Mods

| Change | Original arcade | Setting |
|---|---|---|
| The race Music button skips to the next song | The Music button cycles the volume: Loud, Soft, Off | `[Race] MusicButton = next`, and `volume` for the original |
| Race playlist of 4 songs: Le Mans, Meathook, Tiger Fists, Chow Time | 2 songs, Le Mans and Meathook. Tiger Fists ships on the disc unused. | `[Race] MusicPlaylist = all`, and `original` for 2 songs |
| Chow Time added to the race music, from the NASCAR Racing 1.1 disc | Not part of 2.0.2's race music | Remove `Game\Audio\Music\CHOW_TIME.DJS` |
| Gamepad (Xbox, PlayStation, Switch Pro) and PC wheel support in menus and races | Cabinet wheel, pedals and buttons only | `[Controller]`, `[Frontend]`, `[Wheel]` |
| Menus: a keyboard or D-pad tap moves one step, holding scrolls fast | The cabinet wheel angle sets the speed | None |
| PC wheel with pedals, a gear shifter (H or sequential), a separate rim or button boxes on their own USB cables. Each device's role is set up automatically, and the pedals and shifter type are learned from use. PC pedals are calibrated on the arcade's own screen and saved. | One cabinet wheel with its pedals, shifter and buttons | Automatic. Overrides: `[Wheel] PedalDevice`, `ShifterDevice`, `ShifterMode`, `ButtonBox1`, `Device`. |
| Name-entry screens move one slot per press on keyboard and pad, or follow a PC wheel's position | The cabinet wheel position picks the slot | None |
| Free play, all six tracks enabled, in-race engine volume 100 | Coin-op pricing, tracks set by the operator, engine volume 0 in the shipped database | Operator menu |
| The game never restarts the PC | A nightly "restart scheduled" countdown, then `shutdown.exe -r -f`, which restarts Windows | None |
| Kevin Harvick (#29, Richard Childress Racing) is a playable driver again, in his Shell-Pennzoil car. He shows at driver select, can be switched on or off on the operator Drivers page like every other driver, and shows as car 29 on the high-score screens. | Removed: skipped at driver select. The Drivers page always showed "Disable" and refused changes. Car 29 was a fantasy AI car (#100 "Drafty", GlobalVR #53 paint), shown as 100 in high scores. | Operator menu, Drivers page: set Harvick to Disable for the original line-up. Scenes `NASCAR_Selection.am`, `NASCAR_Attract.am`, `OPERATOR_MENUS\Operator.am` patched. `29.veh` edited. `29-01.dds` rebuilt by the installer from the menus' own picture of his car (`Shell\Textures\CarSkin29.jpg`). Originals kept as `*.oem`. A custom `29-01.dds` is left alone. |

Tiger Fists is mastered about 15 dB quieter than the other songs, so it is hard to hear over the engine.

### Custom race music

With `MusicPlaylist = all`, any of the four song files in `Game\Audio\Music` can be replaced: `LE MANS.DJS`, `MEATHOOK.DJS`, `TIGER FISTS.DJS`, `CHOW_TIME.DJS`.

A `.DJS` file is an MP3 with a different extension. Rename `song.mp3` to one of those names.

- Remove embedded cover art first. A large picture in the tag makes the song play silent.
- Keep the loudness similar to the other songs.

Tested: MP3, 44.1 kHz, stereo, 128 and 320 kbps. Not tested: 48 kHz, mono, variable bitrate.

### Fixes

| Problem on a PC | Fix | Setting or switch |
|---|---|---|
| Front end froze on Alt+Tab. DirectDraw exclusive full screen deadlocks on Windows 10 and 11. | The player uses its OpenGL renderer: `HKCU\Software\Anark\Client\4.0\Preferences ClientRenderer = 2` | None |
| Front end showed no picture, only "Please Wait" with music playing, where Windows offers only software OpenGL. Example: a screen driven by a virtual display adapter. | The launcher tests OpenGL as the front end does, at every start. Hardware OpenGL 1.3 or newer means OpenGL, otherwise Direct3D. | `[Display] Renderer = auto`, or `opengl` / `directx` to force one |
| OpenGL greyed out on drivers that list only the newer extension names | The shim adds the old names (`GL_EXT_bgra`, `GL_WIN_swap_hint`, `GL_ARB_multitexture`, `GL_EXT_texture_env_combine`) for hardware OpenGL 1.3 or newer, which has these built in. *In memory.* | `GVRIOSHIM_NO_GL_FIX=1` turns it off |
| Start-up took about 23 s: a fixed 15 s wait for the cabinet's I/O board to boot | `Shell.am`: wait 0.5 s, the same as NASCAR Racing 1.1. Start to attract takes about 9 s. | None |
| Start-up froze when another window had the focus during loading | Cabinet display tweaks (`nvcpl`, `GammaSet`) are skipped and no longer pause the player (`Shell.am`) | None |
| Attract loop crashed at its third slide | The shim answers the IO-board messages a PC has no board for | None |
| Attract script error every frame, because cabinet-to-cabinet linking is not installed | `NASCAR_Attract.am` checks for a missing packet | None |
| Menu music silent: cabinet path `C:\NASCAR\Shell\Audio` | Not fixed yet: `NASCAR_Selection.am` has to point at the install folder | None |
| Meathook played as silence, because of an 18 KB album picture inside the file | Picture removed from the file's tag. Music and file size are unchanged. | None |
| Steering pulled to one side, and half throttle without a wheel | The shim answers the steering and pedals from the keyboard or pad | None |
| Menus scrolled by themselves without a cabinet wheel | The shim owns the menu selection | None |
| Pad buttons ignored, because 2.0.2's input DLL overwrites them | The shim adds them where 2.0.2 reads the keyboard | None |
| Operator menu slow: a 1 s wait for a coin monitor per query | `NASCARPlugIn.dll` patched in the file: no wait | None |
| Pricing and billing loop | Database built from the 2.0.2 disc with operator pricing and free play | None |
| Front end crashed, or never showed the attract, on Windows set to a day/month date format. Examples: Dutch, German, Japanese. | The shim runs the front end with US region settings. *In memory.* | `GVRIOSHIM_NO_LOCALE_FIX=1` turns it off |
| Menus went black, or stayed on "Please Wait", a few seconds after start on PCs where the Windows event source `GVR` was never registered. The database layer logs to the Windows Application log, and .NET 1.1 tried to create the missing source, which needs administrator rights. | The installer registers the event source `GVR` (Application log, .NET 1.1's message file). The launcher log warns when it is missing. | None |
| Front end needs Anark's AKLog2 logger, which never shipped, and crashed at start without it | A stand-in `AKLogShim.dll` is registered. It answers only this front end, so NASCAR Racing 1.1 on the same PC is not affected. | None |
