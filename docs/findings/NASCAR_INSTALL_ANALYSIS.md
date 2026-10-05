# What the OEM NASCAR installer does — and what we refuse to do

Same analysis as `GAME_ONLY_INSTALL_ANALYSIS.md` in the parent project, for the NASCAR
cabinet media. Source: `GAME ISO EXTRACTED\GVRSETUP.INI`, `Setup.ini`, `setup.inx`
strings, `Registry\*.reg`, `Config\*.bat`, `CloseApps\*`.

---

## 1. The OEM flow

`autorun.inf` → `Setup.exe` (InstallShield 12, script `setup.inx`) which:

1. **Checks `RUNTIMEOEMREV`** — the XP-Embedded revision string under
   `HKLM\System\CurrentControlSet\Control\Session Manager\Environment`. Ordinary Windows
   does not have it, so the OEM installer stops. (Same gate as the NFSU discs.)
2. **Checks `[GAMEINSTALLED]`** — `C:\NASCAR\Game\NASCAR_GVR.exe`.
3. **Checks `[MINIMUMOS]`** — OSName `NASCAR`, OSDate `20070115`, OSVersion `1.0.0`, i.e. it
   expects to be running on a cabinet restored from a GVR recovery disc.
4. Copies the five file groups (`NASCAR`, `Hercules`, `GVRPLUS`, `GVR`, `WINDOWS`).
5. Imports `Registry\*.reg` — **all nine**, by file mask, via `RegEdt32.exe`. The key-by-key
   inventory, the boot chain, and `PGA.msi`'s registry side effects are in
   `NASCAR_AUTOSTART_AND_INSTALL_REGISTRY.md` §B.
6. Runs everything in `GVRSETUP.INI [LAUNCHAPPS]`, in order:

| # | Action | Verdict |
|---|---|---|
| 0 | `wscript Config\Shortcut.wsf` — creates Start-menu `gvrShutdown.lnk` (Close Apps) and `gvrStartup.lnk` (`GVRBoot.exe`), **and** the All Users **Startup** autostarts `LoadingShell.lnk` and `HideTaskbar.lnk` | cabinet autostart + operator tooling — **skip** |
| 1 | `C:\GVR\GVR_Tools\SetVolume.exe 50` — forces system mixer volume | **skip** (changes the machine's audio) |
| 2 | `Config\nascarfw.exe` — wraps `gvrNetworkConfig.bat` | **skip** (see below) |
| 3 | `rundll32 NvCpl.dll,dtcfg setdvc all 20` — NVIDIA Digital Vibrance | **skip** (needs legacy NvCpl; the NFSU work already showed this popup path is a nuisance) |
| 4 | `msiexec /i DbSetup\PGA.msi /quiet` — installs the GvrPlus database component | optional, `-InstallGvrPlus` only |
| 5 | `pathman.exe /as C:\NASCAR\SHELL\BIN` — appends to the **machine** PATH | **skip by default** (`-AddShellBinToPath` to opt in) |
| 6 | `DbSetup\Nascar_db.exe` in `C:\GvrPlus\4\schema\game` — loads schema/pricing tables | optional, shell-only |

### What `gvrNetworkConfig.bat` does

* `netsh interface ip set address/dns/wins local source=dhcp` — **overwrites the machine's
  network configuration**
* `netsh firewall set opmode mode=enable` + disables notifications
* opens UDP 22444, 22643, 9842, 22544 ("NASCAR1..4") for cabinet linking

None of this is needed for single-player. It is destructive to any static IP setup, so the
installer never runs it. If you later want cabinet-to-cabinet linking, open those four UDP
ports yourself — they are documented here so you don't have to dig them out again.

---

## 2. Registry payloads we never import

| File | Why it is refused |
|---|---|
| `Startup.reg` | Adds `Run\GVRHercules = c:\hercules\gvrboot.exe` — the cabinet boot chain starts on every login. |
| `HerculesCab.reg` | Wires the whole monitor stack: `GVRDongleMonitor`, `GVRCoinMonitor`, `GVRStallMonitor`, `GVRCacheWarmer`, `GvrCoinSound`, `GvrVolumeMonitor`, and auto-launch of `AMPlayer.exe shell.am -v FULL -cabinet`. It also pins the system volume (`GVRVolumeMonitor\Volume=0x3a`) and coin-meter masks. |
| `ConfigEventLog.reg` | Sets `Retention=0` on the Application, Security **and** System event logs, in both `CurrentControlSet` and `ControlSet002`. That is a machine-wide logging policy change with no gameplay value. |
| `tsunami.reg` | Points at the Tsunami motion base (`MotionType=4`, `motionLib.dll`, `tsumo` tuning). `motionLib.dll` imports `cbw32.dll` (Measurement Computing InstaCal); without that hardware/driver it is a liability. |
| HKCU cursor block inside `NASCAR.reg` | Replaces the machine's mouse cursor scheme with `GvrCurs` cabinet cursors. Cosmetic, global, and annoying to undo — opt-in via `-InstallCabinetCursors`. |
| `Schema.reg` `WebServerIP=192.168.1.39` | A GlobalVR dev-network address. Meaningless outside their LAN. |

## 3. Cabinet behaviours that simply do not exist in our install

* No shell replacement, no `HideTaskBar.exe`, no `GammaSet.exe`, no forced display mode
  (`sv1360x768x32x60.exe` / `SetVRes.exe` ship on the disc but are never invoked).
* No auto-restart / watchdog (`RestartCabinet.exe`, `GVRCrashMonitor`).
* No coin/dongle monitors, no card dispenser, no operator menus wired to startup.
* No `CleanVss.bat` (`del /q /f /s vssver.scc` from `C:\` down — a recursive delete across
  the whole drive, included on the disc as a build-cleanup leftover). **Never run it.**
* No firewall/DHCP/volume/vibrance changes.
* No PATH edit by default: the one thing PATH was needed for — `NASCAR_GVR.exe`'s static
  import of `GvrIO.dll` from `Shell\bin` — is solved by copying the GvrIO DLL set next to
  the executable instead.

## 4. What our installer does instead

1. Copy `NASCAR\Game` and (optionally) `NASCAR\Shell` under a chosen install root.
2. Copy the GvrIO plug-in set (`GvrIO`, `USBIOExtreme`, `GvrParseXml`, `GvrSound`,
   `GvrLinking`, `Dongle`, `Steering`, `Motion`) beside `NASCAR_GVR.exe`.
3. Copy the MSVC 7.1 runtimes (`msvcr71`, `msvcp71`, `MFC71`, `msvcr70`) beside the
   executables — **not** into `system32` unless `-InstallSystemDlls` is passed.
4. Write only the game-relevant registry values, in both the 32- and 64-bit views, with all
   paths repointed at the actual install root.
5. Generate `Play-NASCAR.cmd` (and `Play-NASCAR-Shell.cmd`) plus a desktop shortcut.
6. Verify the required files exist and print the dongle caveat.

Everything else from the OEM flow is either opt-in behind an explicit switch or absent.
