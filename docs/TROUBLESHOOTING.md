# Troubleshooting

Find your problem below and try the fix. Most fixes are one line in `nascar_settings.ini`, in the install folder. Save the file and start the game again.

If nothing here helps, see [Report a problem](#report-a-problem) at the bottom.

**Contents**

- [Installing](#installing)
- [The game does not start](#the-game-does-not-start)
- [The menus](#the-menus)
- [The race](#the-race)
- [Controllers and wheels](#controllers-and-wheels)
- [Multiplayer](#multiplayer)
- [Report a problem](#report-a-problem)

## Installing

### "Windows protected your PC"

Setup is not signed, so Windows warns about it. Click **More info**, then **Run anyway**.

### The antivirus blocks or deletes Setup

Download the `.zip` from the same release instead. Unzip it with right-click > **Extract All**. Then double-click `Install.bat`.

### The installer refuses the folder

The game cannot run from some folders. Examples: Program Files, OneDrive, Desktop, Documents, a very long path, or a folder name with letters outside your Windows language.

Pick a plain folder such as `C:\Games`.

### "Entry point ... not found" after installing

The disc is not one of the two supported versions. Only these work:

- NASCAR Racing: v1.1 game disc, part number 050-0136-01
- NASCAR Team Racing: 2.0.2 game disc, build 2.0.2.068

Other discs, such as v1.5, install but do not run.

## The game does not start

### Nothing happens, or the game closes at once

1. Run Setup again and choose the same game. The update checks and repairs the install.
2. If that does not help, your antivirus may have removed a game file. Add the install folder as an exclusion in your antivirus. In Windows Security: **Virus & threat protection** > **Manage settings** > **Exclusions** > **Add a folder**. Then run Setup again.

The game is not a virus. Small files sit between the game and Windows so that an arcade game runs on a PC. Antivirus programs often flag that.

### A message says Windows blocked a file

Windows' Smart App Control blocks unsigned programs, and the game's files are not signed. The message names the blocked file.

The setting is in Windows Security > **App & browser control** > **Smart App Control**. Once it is turned off, Windows can only turn it on again by reinstalling Windows. Decide for yourself if that is acceptable.

### A message says the menus cannot use your graphics card

The message names the likely cause:

| Cause | Fix |
|---|---|
| No graphics driver installed | Install the driver from the NVIDIA, AMD or Intel website. |
| Remote or virtual screen | Close Remote Desktop, streaming or virtual-screen tools. Start the game on a real monitor. |
| Monitor plugged into the motherboard | Plug the monitor cable into the graphics card. |

## The menus

### The menus stay on "Please Wait"

1. Update your graphics driver.
2. In `nascar_settings.ini`, under `[Display]`, try `Renderer = opengl`. If that does not help, try `Renderer = directx`.
3. Still stuck? Set `DDrawCompat = on` under `[Display]`. It turns on a compatibility helper for the menus.

Set the values back to `auto` if they do not help.

### The menus freeze after Alt+Tab

Use the borderless window, which is the default: `Borderless = true` under `[Display]`. If the menus still freeze, set `Renderer = opengl`.

### A texture I replaced does not show

Open `LOG\gvrmenutextures.log` in the install folder. It lists each replaced texture, and why one was skipped. More in [MENU_TEXTURES.md](MENU_TEXTURES.md).

### The menus are stuck on the "Game crashed" screen

In NASCAR Racing, wait a few seconds. The screen returns to the attract on its own.

## The race

### The race text is too small or too big

The race text grows with your screen by default. To change it, set `HudText` under `[Display]`:

- `auto`: grows with your screen
- `original`: the arcade's size
- a number such as `1.3`: that many times the arcade's size

### The race is slow on a laptop

Laptops with two graphics chips should use the faster one. The default `Gpu = auto` under `[Display]` does that. If the race is still slow, set `Gpu = high`.

### The picture is the wrong size

Set `Width` and `Height` under `[Display]`. `0` means the size of your screen.

### The race crashes with a custom paint job

Keep `AITextureSize = 0` under `[Graphics]`. That is the default.

## Controllers and wheels

### Gas and brake are the wrong way round

Set `SwapPedals = true` under `[Wheel]`.

### The pedals do not reach full gas or full brake

Calibrate them again. Open the operator menu with `O`, go to **Control Test**, then hold **Start + Music**. Press each pedal all the way down once.

### The wheel, pedals or shifter are not recognised

Plug everything in before you start the game. The game sets up each device by itself.

If a device is still wrong, set `Log = true` under `[Debug]` and start a race. In the `LOG` folder, the `controls:` line shows what each device became. More in [WHEELS.md](WHEELS.md).

### A button does the wrong thing

The button map is under `[Controller]` for the race and `[Frontend]` for the menus. Every action is listed in [findings/NASCAR_CONTROLS.md](findings/NASCAR_CONTROLS.md).

## Multiplayer

### Nobody shows up in the Lobby

Other players must be online at the same time, in the same room. Check that `Mode = lobby` under `[Multiplayer]`. Friends who want a private race set the same `Room` name.

### "Connection lost" right after the finish

This is normal. The hosting player's game ends the session about 20 seconds after that player finishes. A player still racing then sees "Connection lost". The arcade did the same.

More in [MULTIPLAYER.md](MULTIPLAYER.md).

## Report a problem

1. In `nascar_settings.ini`, set `Log = true` under `[Debug]`.
2. Start the game and make the problem happen once.
3. Zip the `LOG` folder in the install folder.
4. If the problem is with installing, also take `install.log` from `%TEMP%\NASCAR_GVR_Install`.
5. Post both on [GitHub Issues](https://github.com/iGThomas/NASCAR_GVR/issues) or on [Discord](https://discord.gg/h7tBkSMbeB). Say which game, what you did and what happened.

Set `Log = false` again for normal play.
