# What the OEM NASCAR installer does, and what this install refuses to do

This page lists what the original cabinet installer on the NASCAR media does. It also lists which
parts of it the game-only install leaves out. Sources: `GAME ISO EXTRACTED\GVRSETUP.INI`,
`Setup.ini`, `setup.inx` strings, `Registry\*.reg`, `Config\*.bat`, `CloseApps\*`.

---

## 1. The OEM flow

`autorun.inf` starts `Setup.exe`, which is InstallShield 12 running the script `setup.inx`. The
script does the following:

1. It checks `RUNTIMEOEMREV`. This is the XP-Embedded revision string under
   `HKLM\System\CurrentControlSet\Control\Session Manager\Environment`. Ordinary Windows does not
   have it, so the OEM installer stops.
2. It checks `[GAMEINSTALLED]`, which is `C:\NASCAR\Game\NASCAR_GVR.exe`.
3. It checks `[MINIMUMOS]`: OSName `NASCAR`, OSDate `20070115`, OSVersion `1.0.0`. So it expects
   to run on a cabinet restored from a GVR recovery disc.
4. It copies the five file groups (`NASCAR`, `Hercules`, `GVRPLUS`, `GVR`, `WINDOWS`).
5. It imports **all nine** `Registry\*.reg` files by file mask, through `RegEdt32.exe`.
   `NASCAR_AUTOSTART_AND_INSTALL_REGISTRY.md` §B lists every key, the boot chain and the
   registry side effects of `PGA.msi`.
6. It runs everything in `GVRSETUP.INI [LAUNCHAPPS]`, in order:

| # | Action | Verdict |
|---|---|---|
| 0 | `wscript Config\Shortcut.wsf` creates the Start-menu shortcut `gvrShutdown.lnk` (Close Apps). It creates `gvrStartup.lnk`, which starts `GVRBoot.exe`. It also adds the All Users Startup autostarts `LoadingShell.lnk` and `HideTaskbar.lnk`. | Skip. Cabinet autostart and operator tooling. |
| 1 | `C:\GVR\GVR_Tools\SetVolume.exe 50` forces the system mixer volume. | Skip. It changes the machine's audio. |
| 2 | `Config\nascarfw.exe` wraps `gvrNetworkConfig.bat`. | Skip. See below. |
| 3 | `rundll32 NvCpl.dll,dtcfg setdvc all 20` sets NVIDIA Digital Vibrance. | Skip. It needs the legacy NvCpl. On a PC without it, this step only shows a RunDLL error popup. |
| 4 | `msiexec /i DbSetup\PGA.msi /quiet` installs the GvrPlus database component. | Optional, only with `-InstallGvrPlus`. |
| 5 | `pathman.exe /as C:\NASCAR\SHELL\BIN` appends to the machine PATH. | Skipped by default. `-AddShellBinToPath` opts in. |
| 6 | `DbSetup\Nascar_db.exe` in `C:\GvrPlus\4\schema\game` loads the schema and pricing tables. | Optional, shell only. |

### What `gvrNetworkConfig.bat` does

* `netsh interface ip set address/dns/wins local source=dhcp` overwrites the machine's network
  configuration.
* `netsh firewall set opmode mode=enable` turns the firewall on. The script also disables its
  notifications.
* It opens UDP 22444, 22643, 9842 and 22544 ("NASCAR1..4") for cabinet linking.

Single-player needs none of this. The script breaks any static IP setup, so the installer never
runs it. For cabinet-to-cabinet linking later, open those four UDP ports yourself. They are listed
here so you do not have to dig them out again.

---

## 2. Registry payloads never imported

| File | Why it is refused |
|---|---|
| `Startup.reg` | It adds `Run\GVRHercules = c:\hercules\gvrboot.exe`. The cabinet boot chain would then start on every login. |
| `HerculesCab.reg` | It wires the whole monitor stack: `GVRDongleMonitor`, `GVRCoinMonitor`, `GVRStallMonitor`, `GVRCacheWarmer`, `GvrCoinSound` and `GvrVolumeMonitor`. It also auto-launches `AMPlayer.exe shell.am -v FULL -cabinet`. It pins the system volume (`GVRVolumeMonitor\Volume=0x3a`) and sets coin-meter masks. |
| `ConfigEventLog.reg` | It sets `Retention=0` on the Application, Security and System event logs. It does this in both `CurrentControlSet` and `ControlSet002`. That changes the logging policy of the whole machine and adds nothing to gameplay. |
| `tsunami.reg` | It points at the Tsunami motion base (`MotionType=4`, `motionLib.dll`, `tsumo` tuning). `motionLib.dll` imports `cbw32.dll` from Measurement Computing InstaCal. Without that hardware and driver, it is a liability. |
| HKCU cursor block inside `NASCAR.reg` | It replaces the machine's mouse cursor scheme with the `GvrCurs` cabinet cursors. The change is cosmetic, global and annoying to undo. `-InstallCabinetCursors` opts in. |
| `Schema.reg` `WebServerIP=192.168.1.39` | This is a GlobalVR dev-network address. It means nothing outside their LAN. |

## 3. Cabinet behaviours that simply do not exist in this install

* No shell replacement, no `HideTaskBar.exe`, no `GammaSet.exe` and no forced display mode.
  `sv1360x768x32x60.exe` and `SetVRes.exe` ship on the disc but are never run.
* No auto-restart or watchdog (`RestartCabinet.exe`, `GVRCrashMonitor`).
* No coin or dongle monitors, no card dispenser, and no operator menus wired to startup.
* No `CleanVss.bat`. **Never run it.** It runs `del /q /f /s vssver.scc` from `C:\` down, which
  is a recursive delete across the whole drive. It is on the disc as a leftover from build cleanup.
* No firewall, DHCP, volume or vibrance changes.
* No PATH edit by default. PATH was needed for one thing only: `NASCAR_GVR.exe` imports
  `GvrIO.dll` statically from `Shell\bin`. The install copies the GvrIO DLL set next to the
  executable instead.

## 4. What the installer does instead

1. Copy `NASCAR\Game`, and optionally `NASCAR\Shell`, under a chosen install root.
2. Copy the GvrIO plug-in set beside `NASCAR_GVR.exe`: `GvrIO`, `USBIOExtreme`, `GvrParseXml`,
   `GvrSound`, `GvrLinking`, `Dongle`, `Steering`, `Motion`.
3. Copy the MSVC 7.1 runtimes (`msvcr71`, `msvcp71`, `MFC71`, `msvcr70`) beside the executables.
   They go into `system32` only when `-InstallSystemDlls` is passed.
4. Write only the game-relevant registry values, in both the 32-bit and 64-bit views. All paths
   point at the actual install root.
5. Generate `Play-NASCAR.cmd` (and `Play-NASCAR-Shell.cmd`) plus a desktop shortcut.
6. Check that the required files exist, and print the dongle caveat.

The rest of the OEM flow is either absent or opt-in behind an explicit switch.
