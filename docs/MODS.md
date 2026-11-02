# Changes compared with the original arcade

What the packages change on top of the original cabinet software. **Mods** change gameplay or
content on purpose; **fixes** are what a PC needs to run the game at all. Where a change has a
switch, it is in `nascar_settings.ini`; the original behaviour is always one setting away.
Executables on disk stay original unless the table says otherwise; patches marked *in memory*
are applied by the `GvrIO.dll` shim while the game runs.

## NASCAR Team Racing 2.0.2

### Mods

| Change | Original arcade | Setting |
|---|---|---|
| Race Music button skips to the next song | Music button cycles the volume (Loud / Soft / Off) | `[Race] MusicButton = next` (`volume` = original) |
| Race playlist of 4 songs: Le Mans, Meathook, Tiger Fists, Chow Time | 2 songs (Le Mans, Meathook); Tiger Fists ships on the disc unused | `[Race] MusicPlaylist = all` (`original` = 2 songs) |
| Chow Time added to the race music (from the NASCAR Racing 1.1 disc) | Not part of 2.0.2's race music | Remove `Game\Audio\Music\CHOW_TIME.DJS` |
| Gamepad (Xbox, PlayStation, Switch Pro) and PC wheel support in menus and races | Cabinet wheel, pedals and buttons only | `[Controller]`, `[Frontend]`, `[Wheel]` |
| Menus: keyboard / D-pad tap moves one step, holding scrolls fast | Cabinet wheel angle sets the speed | — |
| PC wheel with pedals, a gear shifter (H or sequential), a separate rim or button boxes on their own USB cables: each device's role set up automatically (pedals and shifter type learned from use), pedals calibrated on the arcade's own screen with PC pedals, saved | One cabinet wheel with its pedals, shifter and buttons | Automatic; overrides `[Wheel] PedalDevice`, `ShifterDevice`, `ShifterMode`, `ButtonBox1`, `Device` |
| Name-entry screens move one slot per press (keyboard, pad) or follow a PC wheel's position | Cabinet wheel position picks the slot | — |
| Free play, all six tracks enabled, in-race engine volume 100 | Coin-op pricing, tracks per operator, engine volume 0 in the shipped database | Operator menu |
| The PC is never restarted by the game | Nightly "restart scheduled" countdown, then `shutdown.exe -r -f` (restarts Windows) | — |

Note: Tiger Fists is mastered about 15 dB quieter than the other songs and is hard to hear over
the engine.

### Custom race music

With `MusicPlaylist = all`, any of the four song files in `Game\Audio\Music` can be replaced:
`LE MANS.DJS`, `MEATHOOK.DJS`, `TIGER FISTS.DJS`, `CHOW_TIME.DJS`. A `.DJS` file is an MP3 with a
different extension — rename `song.mp3` to one of those names. Remove embedded cover art first
(a large picture in the tag makes the song play silent) and keep the loudness similar to the
others. Tested: MP3, 44.1 kHz, stereo, 128 and 320 kbps. Not tested: 48 kHz, mono, variable bitrate.

### Fixes

| Problem on a PC | Fix |
|---|---|
| Front end froze on alt-tab (DirectDraw exclusive full screen deadlocks on Windows 10/11) | Player uses its OpenGL renderer (`HKCU\Software\Anark\Client\4.0\Preferences ClientRenderer = 2`) |
| Front end showed no picture (only "Please Wait", music playing) where Windows offers only software OpenGL, e.g. a screen driven by a virtual display adapter | Launcher tests OpenGL as the front end does at every start: hardware OpenGL 1.3+ = OpenGL, else Direct3D (`[Display] Renderer = auto`; `opengl` / `directx` force one) |
| OpenGL greyed out on drivers that list only the newer extension names | Shim adds the old names (`GL_EXT_bgra`, `GL_WIN_swap_hint`, `GL_ARB_multitexture`, `GL_EXT_texture_env_combine`) for hardware OpenGL 1.3+, which has these built in (*in memory*; `GVRIOSHIM_NO_GL_FIX=1` turns it off) |
| Start-up took ~23 s: a fixed 15 s wait for the cabinet's I/O board to boot | `Shell.am`: wait 0.5 s (same as NASCAR Racing 1.1); start to attract ~9 s |
| Start-up froze when another window had the focus during loading | Cabinet display tweaks (`nvcpl`, `GammaSet`) are skipped and no longer pause the player (`Shell.am`) |
| Attract loop crashed at its third slide | Shim answers the IO-board messages a PC has no board for |
| Attract script error every frame (cabinet-to-cabinet linking is not installed) | `NASCAR_Attract.am` checks for a missing packet |
| Menu music silent (cabinet path `C:\NASCAR\Shell\Audio`) | Not in the package yet: `NASCAR_Selection.am` has to point at the install folder |
| Meathook played as silence (an 18 KB album picture inside the file) | Picture removed from the file's tag; music and file size unchanged |
| Steering pulled to one side, half throttle without a wheel | Shim answers the steering and pedals from the keyboard / pad |
| Menus scrolled by themselves without a cabinet wheel | Shim owns the menu selection |
| Pad buttons ignored (2.0.2's input DLL overwrites them) | Shim adds them where 2.0.2 reads the keyboard |
| Operator menu slow (1 s wait for a coin monitor per query) | `NASCARPlugIn.dll` patched (file): no wait |
| Pricing / billing loop | Database built from the 2.0.2 disc with operator pricing and free play |
| Front end crashed (or never showed the attract) on Windows set to a day/month date format, e.g. Dutch, German, Japanese | Shim runs the front end with US region settings (*in memory*; `GVRIOSHIM_NO_LOCALE_FIX=1` turns it off) |
| Nightly restart prompt restarted the PC | `NASCARPlugIn.dll` patched (file): restart check always "not yet"; shim also blocks `shutdown.exe` and the Windows restart calls (*in memory*) |
| Database password of the original cabinet written to the log | Password masked in `gvrsqlite.log` |
| Menus went black (or stayed on "Please Wait") a few seconds after start on PCs where the Windows event source `GVR` was never registered: the database layer logs to the Windows Application log, and .NET 1.1 tried to create the missing source, which needs administrator rights | The installer registers the event source `GVR` (Application log, .NET 1.1's message file); the launcher log warns when it is missing |
| Front end needs Anark's AKLog2 logger, which never shipped (crashed at start without it) | A stand-in `AKLogShim.dll` is registered; it answers only this front end, so NASCAR Racing 1.1 on the same PC is not affected |

### Both games

| Problem on a PC | Fix |
|---|---|
| DirectX device list included detached and non-display devices (an inactive virtual monitor could be picked) | Only attached displays are offered; every device and the one the renderer opens are written to the log (*in memory*; `GVRIOSHIM_NO_DD_FIX=1` turns it off) |
| PC wheel that does not report itself as a wheel (direct-drive bases such as MOZA) steered in the menus but not in the race: exclusive access for force feedback was refused and the wheel was then not read at all | Wheel read shared when exclusive access is refused (steering kept, no force feedback from the shim in that program) |
| A generic USB H-shifter acted as extra buttons (gears changed the camera or shifted up / down) until it was recognised, and each race had to recognise it again | Recognised from two held gears even when it reports unused axes or a held mode switch; remembered in `nascar_calibration.ini` `[Shifter]` (`[Wheel] ShifterDevice = none` turns shifters off) |
| Transmission screen ignored a PC wheel when the first menu message came before the wheel was found | Menu selection moves to the shim as soon as a PC wheel or pedal box is found |
| Front end start-up could wait for a slow controller scan (some USB devices answer slowly) | The scan runs in the background; start-up waits for it at most 3 s |
| Front end crashed while closing (exit code 0xC0000005 in the log) | Shim's I/O-board watchdog stops when `GvrIO_oem.dll` is unloaded |

## NASCAR Racing 1.1

Gamepad and PC wheel support, the horn stops on release, the private registry and the SQLite
database are described in `docs/findings/NASCAR_FINDINGS.md`.

| Problem on a PC | Fix |
|---|---|
| Attract never loaded on Windows set to a day/month date format, e.g. Dutch, German, Japanese | Shim runs the front end with US region settings (*in memory*; `GVRIOSHIM_NO_LOCALE_FIX=1` turns it off) |
| Nightly restart prompt restarted the PC | `NASCARPlugIn.dll` patched (file): restart check always "not yet"; shim also blocks `shutdown.exe` and the Windows restart calls (*in memory*) |
| Race picture blurry on screens wider than 4096 pixels (e.g. 5120x1440) | Shim accepts display modes up to 16384 pixels (*in memory*) |
| PC wheel with pedals, a gear shifter, a separate rim or button boxes on their own USB cables ignored (only one controller was read) | All read together and set up automatically (overrides under `[Wheel]`) |
| Database password of the original cabinet written to the log | Password masked in `gvrsqlite.log` |
| Menus stuck on a frozen "Please Wait" (music playing) on PCs where the game's own Direct3D 7 test fails: it then greys out Direct3D AND OpenGL and draws in software | Launcher repeats that test before every start: passes = Direct3D 7 as on the cabinet, fails = OpenGL (`[Display] Renderer = auto`; `opengl` / `directx` force one) |
| With NASCAR Team Racing installed on the same PC, the front end loaded Team Racing's AKLog2 stand-in | Shim answers "not registered" for AKLog2 in this front end, as on a PC without it (*in memory*) |

