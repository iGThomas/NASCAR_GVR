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

