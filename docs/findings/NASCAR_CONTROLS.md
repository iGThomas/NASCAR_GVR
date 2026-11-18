# Controls: NASCAR Racing and NASCAR Team Racing

This page lists every control in both games: keyboard, cabinet buttons, pads and wheels. Each control is shown as the game's config files store it. It is for players who want to remap a button, and for developers who want to know how input works inside the game.

The list was taken from the installed config files (`D:\Games\NASCAR`) on 2026-10-03. It was corrected on 2026-10-04 (`NASCAR_FINDINGS.md` §14).

Input reaches the game through two layers:
* GvrIO carries every cabinet control to both the shell and the race. The shell is the Anark front end. GvrIO covers the wheel and pedals as analog axes, and the buttons and shifter (`GvrIO.xml`).
* The race's own `.CTL` file only adds keyboard keys, such as clutch, pit menu and replay. Its "controller 1" lines are never read, because this build opens no DirectInput joystick.

Xbox and PlayStation pads plug into GvrIO, like the cabinet does: see section 4. To change a binding, see section 3.

## 1. Shell / cabinet buttons — `Game\config\GvrIO.xml`

On a PC, the keyboard column is the one that works. The board numbers are the cabinet's GvrIO-mini inputs. The `Steering` column uses the older wheel board numbering.

| Function | Keyboard | GvrIO-mini input | Steering-board input | Script event |
|---|---|---|---|---|
| Start | `S` | 0 | 20 | `onGvrStartPressed` |
| Look back | `L` | 1 | 9 | `onGvrLookBackPressed` |
| Look back 2 | none | 20 | none | `onGvrLookBack2Pressed` |
| Music | `M` | 2 | 8 | `onGvrMusicPressed` |
| View | `V` | 3 | 0 | `onGvrViewPressed` |
| Volume up | none | 10 | none | `onGvrVolumeUpPressed` |
| Volume down | none | 11 | none | `onGvrVolumeDownPressed` |
| Operator (service menu) | `O` | 12 | none | `onGvrOperatorPressed` |
| Service | none | 13 | none | `onGvrServicePressed` |
| Coin 1 / Coin 2 | none | 8 / 9 | none | `onGvrCoin1Pressed` / `onGvrCoin2Pressed` |
| Shifter 1 to 4 | `T` `Y` `U` `I` | 16 to 19 | none | `onGvrShifter1..4Pressed` |
| Menu forward / back (race: gas / brake) | `R` / `C` | none | none | `onGvrForwardPressed` / `onGvrBackPressed` |
| Menu left / right (race: steer) | `D` / `G` | none | none | `onGvrLeftPressed` / `onGvrRightPressed` |
| Quit | `Q` | none | none | `onGvrQuitPressed` |
| Motion disable | `A` | none | none | `onGvrMotonDisablePressed` (sic) |
| Test key | `X` | none | none | `onGvrXKeyPressed` |

On the cabinet, the steering wheel makes the selection (`onSelectionChanged`, GvrIO axis X). On the motion-error slide, Start + Music continues without motion.

Shell screens on a PC (2026-10-04):

| Screen | Keys |
|---|---|
| Attract ("Press Start") | `S` = start, `O` = operator menu |
| Car select ("Start Your Engines") / transmission / track | `←` or `D` = steer left. `→` or `G` = steer right. `S` = select, or the gas pedal on a wheel. With no wheel attached, the shell's GvrIO shim feeds the steering keys to `Gvr.AxisX`. |
| Operator menu (legend on screen) | `V` View = up, `M` Music = down, `S` Start = select, `L` Look Back = cancel/back |

The steering keys only act while the shell window has focus. A real wheel or pedal device always takes priority over the keyboard.

## 2. Race controls — `.CTL` files

Each binding is a pair `(device, code)`. Device `0` is the keyboard, and the code is a DirectInput scan code. Device `1` is the first game controller (wheel or pad). `(0, 89)` means unbound.

The live set is `Game\Save\GvrSinglePlayer\GvrSinglePlayer.CTL`. This is the file the cabinet uses.

| Control | Binding |
|---|---|
| Accelerate | controller axis input 4 |
| Brake | controller axis input 3 |
| Steer left / right | controller axis inputs 2 / 1 |
| Shift up / down | controller buttons 20 / 19 |
| Clutch in | `Q` |
| Pit menu up / down / inc / dec | `↑` `↓` `→` `←` |
| Traction-control override | `O` |
| Brake bias forward / rearward | `[` / `]` |
| Rear look | `V` |
| Look left / right | `C` / `B` |
| Instant replay | `R` |
| Pause | `P` |
| Restart race | `W` |
| Vehicle labels | `Tab` |
| Horn | `J`. This install binds it (`Control - Horn="(0, 36)"`). The OEM file leaves it unbound. The `.CTL` Horn binding is superseded by the horn note at the end of section 4. Not H: H cycles the HUD through normal, hidden and debug overlay. |
| Chase (Swingman) camera | Numpad `4` / `6` turn, `8` / `2` up / down. These are engine keys, not in the file. |
| Popups / standings | `F1` to `F5`, `Space`, `I` / `K`. They show race info, standings, lap times, tires and pit stop. |

**The controller lines are dead on this build** (2026-10-04). The race creates only a DirectInput keyboard, so `(1, …)` bindings are never read. It drives from GvrIO instead. GvrIO gives the cabinet wheel and pedals, or a pad (section 4), as analog axes. It also gives the `GvrIO.xml` keys as digital ramps: `R` gas, `C` brake, `D` / `G` steer.

The gear-select buttons (neutral, reverse, 1st to 7th) are unbound. A manual gearbox uses the cabinet's 4-position shifter: hold `T` `Y` `U` `I` for gears 1 to 4.

The stock keyboard preset is `Game\Options\Basic Settings\Keyboard.CTL`. It binds accelerate `↑`, brake `↓`, steer `←`/`→`, shift up `A`, shift down `Z`, clutch `Q` and pit menu `I` `K` `L` `J`. It also has the same `O` `[` `]` `V` `C` `B` `R` `P` `W` `Tab` as above. It adds push-to-talk `` ` ``, realtime chat `T` and quick chat `1` to `0`.

The other presets in that folder are `Default Wheel.ctl`, `Logitech - MOMO.ctl`, `Microsoft - Wheels - R1.ctl` / `R2.ctl` and `Saitek - R440.CTL`.

The live set tunes the keyboard as follows: steering rate `0.18`, throttle `0.50`, brake `0.50`, clutch `0.25`. Force feedback is off. Per-axis dead zone and sensitivity blocks follow as `Axis [01, nn] ...` lines.

Decoder: `NASCAR\Tools\Decode-Ctl.py <file.CTL>` prints every binding with key names.

## 3. Changing a binding without breaking the cabinet (2026-10-04)

* `GvrIO.xml` keeps the keyboard and the cabinet separate. Every action has its own `<button num=…
  board=…>` line for each board input. It also has its own `<key char=…>` line. All these lines name the same `messageID`/`event`.
  * Editing a `<key>` line never touches the cabinet input. Both work at once.
  * One file serves both processes. `GvrIO_oem.dll` reads `HKLM\SOFTWARE\Gvr\Plus\2.0\Cabinet\GameRoot` and loads `<GameRoot>\Config\GvrIO.xml`. Here GameRoot is `D:\Games\NASCAR\Game\`. The same build of `GvrIO_oem.dll` sits in `Game\` and `Shell\bin\`.
  * `NASCAR_GVR.exe` registers for the `onGvr…ButtonDown/Up` events of View, LookBack, LookBack2, Music, Start, NOS, MotionDisable, Left, Right, Forward and Back. So the race also takes the cabinet buttons from this file.
* The `.CTL` cannot break the wheel. Each `Control - <name>="(device, code)"` holds one binding. The race never reads its controller lines (section 2). The wheel, pedals and pad arrive through GvrIO. So rebinding a `.CTL` action to a key only changes the keyboard. This corrects the 2026-10-04 afternoon note, which assumed controller 1 was the wheel.
* Car-select keyboard steering is hardcoded in the shim, not in a config file. `GvrIOShim.cpp` reads `VK_LEFT`/`D` and `VK_RIGHT`/`G` through `GetAsyncKeyState`. It only runs while OEM GvrIO reports no usable pedal range (max < 40). So a connected wheel switches it off by itself.
* Some letters are bound in both files:
  * `V`: View and Rear Look.
  * `R`: Forward and Instant Replay.
  * `C`: Back and Look Left.
  * `Q` and `O` are also in both, but the race does not listen for Quit or Operator.

  Check both files before giving a key a new job.

## 4. Xbox and PlayStation pads (2026-10-04)

Xbox pads work through XInput. The PS4 DualShock 4 works through raw HID, over USB or Bluetooth. Pad support is built into the `GvrIO.dll` shim (`src\GvrIOShim\GvrIOPad.cpp`).

The sticks and triggers act as the cabinet's analog wheel and pedals. Each button "holds" the cabinet key it stands for in GvrIO's own keyboard. So both programs treat it exactly like the cabinet button.

The button map is in `nascar_settings.ini`: `[Controller]` for the race and `[Frontend]` for the menus. Each line reads `button = action`, for example `Square = shiftup`. `GVRIOSHIM_NO_PAD=1` turns pad support off. How it works: `NASCAR_FINDINGS.md` §14.

| Pad (Xbox) | Race | Menus |
|---|---|---|
| Left stick | steering | car / track select (as the wheel) |
| R2 (RT) / L2 (LT) | gas / brake | gas = select, as the cabinet's pedal |
| Cross (A) | none | select (Start) |
| Circle (B) | horn (the engine's Horn control, as keyboard `J`). Superseded: see the horn note below. | back (Look Back) |
| Square (X) / Triangle (Y) | shift up / down (manual transmission, 4 gears) | none |
| L1 (LB) | look back | none |
| R1 (RB) | camera (View) | none |
| Options (Menu) | Start: put the car back on the track | operator menu |
| D-pad up / down | none / quit: hold 1 s | up / down (View / Music) |
| D-pad left / right | none / music | left / right. Also steers car and track select, and picks Auto / Manual. |
| Right stick | chase-camera turn (numpad 4/6/8/2) | none |

Notes on the layout:
* Not on the pad: an e-brake or nitrous action, because the race has neither. Circle is the horn instead. There is no quit prompt either. The quit is the cabinet's four-button abort, so on the pad you hold D-pad down for a second. The menus use R2 as the gas pedal, not for name-entry accept or backspace.
* The pad has these cabinet functions: the horn, the right-stick chase camera and the 4-speed shifter (4 gears). It also has Start to put the car back on the track, and the four-button abort. These cabinet-only buttons have no pad button: Volume up/down, Service, Coin, Motion disable, LookBack2.
* Some race extras are keyboard-only. They sit in the race's own `.CTL` keyboard, which GvrIO cannot reach. They are clutch, pit menu, traction-control override, brake bias, look left/right, instant replay, pause, restart, vehicle labels and the F1 to F5 popups. The horn and the numpad camera are the two the pad does press there. The horn part is superseded by the horn note below.

Transmission select moves with GvrIO's own selection. It steps on each press of Left or Right. On a keyboard, these are numpad 4 / 6, because the arrow keys' codes map to nothing in GvrIO. On the pad, use D-pad left/right or push the stick over half-way. Confirmed with a PS4 pad over Bluetooth on 2026-10-04 (`NASCAR_FINDINGS.md` §14.6).

2026-10-05: the `<key char="a" name="MotionDisable">` line was removed from `GvrIO.xml`. The OEM file is kept as `GvrIO.xml.oem`. Held, this key shows the race's motion-seat HUD panel. On AZERTY it is the A key, which looked like a stray popup.

**The race rewrites `GvrSinglePlayer.CTL` when it exits.** Edit it only while no race is running.

Horn (final, 2026-10-05): the horn is keyboard J or pad Circle. Both press the cabinet NOS input (`GvrIO.xml` `name="NOS"`). The GvrIO shim stops the horn on release. The `.CTL` `Control - Horn` does nothing in this build and is unbound again. Rows above that say the horn is the `.CTL` Horn on H/J are superseded.

### 4.x Nintendo Switch Pro Controller (2026-10-11)

`GvrIOPad.cpp` reads it through the same raw-HID path as the DS4 (VID `057E`, PID `2009`). The first HID pad found wins. XInput still comes first.

* Wake-up runs in `switch_init` on the discovery thread, before the handle goes to the game threads.
  * The pad is reopened with write access.
  * USB only: it sends output reports `80 02`, `80 03`, `80 02`, `80 04`. These are handshake, 3 Mbit, handshake, USB-only, in SDL's order.
  * Both links: it sends subcommand `03 30` for full report mode and `30 01` for player LED 1. The LED is visible proof that it worked.
  * A subcommand is report `01`, a 4-bit counter, 8 bytes of neutral rumble `00 01 40 40 00 01 40 40`, the id and the arg.
  * Bluetooth is detected by the HID service GUID `00001124` in the device path. Without write access, a Bluetooth pad still works in its simple mode.
* `latch_switch` decodes the reports:

  | Report | Byte | Content |
  |---|---|---|
  | `0x30` / `0x21` | 3 | `Y X B A SR SL R ZR` |
  | `0x30` / `0x21` | 4 | `- + RS LS Home Capture` |
  | `0x30` / `0x21` | 5 | `down up right left SR SL L ZL` |
  | `0x30` / `0x21` | 6 to 8 / 9 to 11 | left / right stick, as two 12-bit values |
  | `0x3F` (simple Bluetooth report) | 1 | `B A Y X L R ZL ZR` |
  | `0x3F` | 2 | `- + LS RS Home Capture` |
  | `0x3F` | 3 | hat |
  | `0x3F` | 4 to 11 | 16-bit LX LY RX RY |

  In the `0x30` / `0x21` stick values, the centre is ~2048 and up is larger. Dead zone 200, full 1500. No factory calibration is read.
* Buttons map by position, like Xbox: B = Cross, A = Circle, Y = Square, X = Triangle, L/R/ZL/ZR = L1/R1/L2/R2, −/+ = Share/Options, Home = PS. Capture = `capture`, which is unbound. The ini accepts the names `l r zl zr
  minus plus home capture`. The names `a b x y` stay the Xbox positions.
* ZL / ZR are digital, so gas and brake are either 0 or 255. `[Controller] Pedals = rightstick` works on any pad. It adds right-stick up/down as gas/brake, taking the max of stick and trigger. It also stops the right stick turning the Swingman camera vertically.
* Verified without hardware: a decoder harness passes, with synthetic `0x30` / `0x21` / `0x3F` reports and a DS4 regression. The shell and the race run with the new DLL. On a working pad, the log shows `pad: Switch Pro Controller over … - full
  report mode requested` and the player-1 LED lights.

### 4.y Steering wheels and force feedback (2026-10-11)

The OEM already supports a PC force-feedback wheel. `Steering.dll` has a DirectInput back end. It is the GvrIO plug-in that `GvrIO.xml`'s `<controller type="Steering">` loads, and it sits next to both programs.

* It enumerates attached force-feedback devices of type `DI8DEVTYPE_DRIVING` (0x16) and opens one exclusive + background.
* It turns autocentre off and creates Spring, Damper, two Sine and a ConstantForce effect.
* Only without such a device does it fall back to its HID protocol for the cabinet's Happ FFB board.
* It reads only the wheel's X axis (`GetDeviceState(0x110)` at `0x10003090`, `lX` as a word). On the cabinet, the pedals come from the I/O board.

The race sends these effects, all to GvrIO device 5. It sends them at setup from `0x45c130`/`0x45c500`, and per frame from `0x45c650`, only when a value changed:
* `0x1d` spring (magnitude, centre offset)
* `0x35` damper
* `0x19` / `0x1b` sine A / B (force, period)

Spring and damper ramp in over `FF Spring Ramp Time` (3 s). The sines are per-tyre surface: grass `0xF`, rumble strip `0x12`, road. They are lerped by speed (`min(1, |v| / FF Speed Throttle 50)`). Collisions (`0x4d5ab6`, `0x4d5cfc`) set the spring centre to +/- Collision Offset with Collision Force for that frame.

Steering.dll maps these effects to DirectInput:
* Spring and damper: `DICONDITION` coefficients 10000, gain = value x 100, spring offset = centre x 100.
* Sines: magnitude 10000, offset -5000, gain = force x 100.

There is no physics steering torque.

The arcade defaults come from the `GvrSinglePlayer.PLR` profile "FF Handling". The operator setting "Steering Feedback Strength" picks Normal or Light (`-handling 0/1`). The values, Normal / Light, are spring 40 / 25, damper 40 / 25, rumble 70-75 / 50-55, grass 21-32 / 11-21, collision cars 25/15 (offset/force) and walls 35/25. The EA `.CTL` FFB block is dead code. The exe holds no DirectInput effect GUIDs.

The shim's part is in `src\GvrIOShim\GvrIOWheel.cpp`, set by `[Wheel]` in `nascar_settings.ini`:
* Input. When no I/O-board pedals exist, the shim already answers msg 5. So the wheel feeds the same `PadState` as the pads.
  * Steering is -127..127, with optional `Rotation` / `DeviceRange` scaling.
  * Gas and brake are 0..255, from separate axes (default `y` / `rz`) or one combined axis. Each pedal's rest value is learned from the first reading, so either direction works.
  * Buttons map to pad positions (`ButtonN = cross`...). DFGT guesses are built in, and every press is logged with its number. The hat is the D-pad.
  * The wheel is found on the pad discovery thread: the first `DI8DEVTYPE_DRIVING`, or a `[Wheel] Device=` name match. The shim opens it with its own `IDirectInput8A` and reads it non-exclusively. So it never conflicts with Steering.dll's exclusive hold.
* Force feedback.
  * `ForceFeedback = arcade` (default) leaves it to Steering.dll, which gives the exact cabinet effects.
  * `shim` uses the shim's own spring + damper, for a wheel Steering.dll does not pick up. The defaults are arcade Normal 40 / 40, set by `ForceSpring` / `ForceDamper` / `ForceGain`. It takes the wheel exclusive + foreground on the process's foreground window.
  * `off` means no shim effects.
* A trap was fixed at the same time. GvrIOPad's shared `GetDeviceState` hook used to treat every read of >= 256 bytes as a keyboard. Steering.dll's 272-byte `DIJOYSTATE2` read would have received the pad's right-stick camera keys as button presses. Now only reads of exactly 256 bytes count, and the shim's own wheel is skipped.
* Known risks:
  1. The shell and the race both load Steering.dll. If the shell keeps the wheel exclusively while the race runs, the race's acquire would fail. The race may then get no force feedback. So force feedback working in the menus does not prove it works in a race.
  2. The DFGT must run in native mode (Logitech G HUB / Gaming Software). Only then does it enumerate as a driving device with separate pedals.
  3. Steering.dll's shell self-test can write `ForceFeedbackBroken` to the NASCAR registry key.

2026-10-12, first real-wheel test (Logitech G29 + Driving Force Shifter, v1.0.8). Steering, the arcade force feedback (via Steering.dll) and the paddle shifters work. **The pedals and the H-shifter did not.**

Pedals: with a wheel attached, Steering.dll reports a calibrated gas range. So the shim's `real_pedals` check passed the OEM's axis values through. But the OEM gas/brake come from the absent I/O board (0). Fix (1.0.9): `wheel_present()`. While the shim's wheel is open, its axes always win (GvrIOShim.cpp msg 5, race and shell).

H-shifter: on G29/G920/G923 the shifter's gears are wheel buttons 12..17 (gear 1..6), and 18 is reverse. New pad actions `gear1`..`gear4` select the arcade's 4-position shifter gear directly. 5th and 6th count as 4th. These actions are the defaults for those wheels. Other wheels use `[Wheel] ButtonN =
gearN`. The log now names each wheel axis the first time it moves (`wheel: axis 'rz' moves ...`). So the log shows which axis is gas, brake or clutch.

Follow-up, same day, v1.0.8: the pedals did work in the menus (driver selection). They stopped once the race (NASCAR_GVR.exe) loaded. Each program makes its own `real_pedals` decision, and only the race's OEM Steering.dll reported a calibrated range. The 1.0.9 `wheel_present()` override covers both.

Paddles "not working" in the race meant automatic transmission. Sequential (paddle) and H-shifter gears only act in manual, chosen on the shell's transmission screen.

2026-10-26: several devices, set up by themselves. Direct-drive rigs split the controls over several USB devices: base, pedal box, shifter, rim, button box. The shim reads them together and decides each role itself. Every `[Wheel]` device setting is only an override.
* Wheel: `Device`. Otherwise a force-feedback driving device, then a force-feedback device that is not a gamepad (a base listed as a joystick), then a driving device. It is never a pedal box or shifter.
* Pedals: `PedalDevice`. Otherwise a device with "pedal" or "LCM" in its name. Otherwise the pedals are learned.
  * For a wheel that is not a known model, every other device is watched. The first device whose axis rests at an end of its range and then travels more than half of it becomes the pedal box. That is a pedal, because stick and wheel axes rest in the middle. It needs at least two such axes.
  * Its pedal axes in order are gas, brake. The clutch is ignored. Rz + Y is read Logitech-style, with gas on Rz.
  * Known models (Logitech, Thrustmaster presets) keep their own pedals, so a USB handbrake never takes over.
* Shifter: `ShifterDevice`. Otherwise a device with "shift", "TH8" or "TSS" in its name. `ShifterMode = auto` learns the type.
  * A button held over 0.4 s is an H-pattern gear: buttons 0..5 = gears 1..4, 4, 4.
  * Two short taps without such a hold make it sequential: 0 / 1 = square / triangle = shift up / down.
  * Until the type is known, a press waits up to 0.4 s. A tap is passed on as a 150 ms press.
* Shifter without "shift" in its name, such as a generic USB shifter. It starts as a button slot.
  * A device becomes the shifter, H-pattern, when its axes never move and it holds one button over 2 s, later another one. That means two gears: rim and box buttons are tapped, and a toggle switch is one button.
  * Buttons already down at the first reading, such as a mode switch, are ignored until released once.
  * The result is saved in `nascar_calibration.ini` `[Shifter]` (VID:PID). So the shell and every race pick it up at once.
  * Named button boxes never become the shifter. `ShifterDevice = none` turns shifters off.
* Buttons: every other controller that is not a gamepad, such as a separate rim or a button box, adds its buttons and hat through the wheel's `ButtonN` map. `ButtonBox1..3` (+ `ButtonBoxKButtonN`) name them.
* Access: a base that Windows lists as a joystick with force feedback gets the shim's own force feedback. Examples are HID PID direct-drive bases, e.g. MOZA, USB VID 346E.
  * This force feedback needs exclusive foreground access. The menus and the race are two programs.
  * The exclusive `Acquire` is refused when the other program or the maker's software still holds the base. Then the wheel is read shared until its window changes, without the shim's force feedback in that program.
  * Before, a refused `Acquire` left the wheel unread. Steering was 0 while separate pedals worked, because pedals are always read shared.
* Discovery runs only on the pad thread. The front end's start-up pedal check waits at most 3 s for its first pass. Menu selection (transmission screen) moves from the OEM to the shim as soon as a PC wheel or pedal box is found. So a slow first scan no longer leaves the transmission screen on the cabinet-wheel path.
* Pedal calibration is the cabinet's own.
  * The I/O board keeps a raw position per pedal (GvrIO msg 6 = `Gvr.AxisRawY/Z`). It also keeps a min / max (`0x39` / `0x3A` = `Gvr.AxisMin/MaxY/Z`) that grows as the pedal moves.
  * `Gvr.ResetCalibration = true` (msg `0x53`) clears it. `Gvr.AxisY/Z` (msg 5) is the calibrated value.
  * `Shell.am` shows `PedalCalibrate` ("Calibrate the accelerator and brake") when max - min < 40. The operator Control Test resets it on Start + Music.
  * For PC pedals, the shim plays the board. It follows the property table of `ZeusIOPlugIn.dll`, whose message ids are identical in 1.1 and 2.0.2. Raw is the travel from the released position, 0..255 of the axis.
  * Min / max are saved in `<install>\nascar_calibration.ini` for the pedal device (`[Pedals] Device`, `GasMin/Max`, `BrakeMin/Max`). So the shell and every race share it, and new pedals start uncalibrated. The shell asks once.
  * Until calibrated, 100 % is the deepest point reached in that race, at least half the axis. Pads and combined Logitech pedals keep the former fixed 0..255 answer, with no screen.
  * The maker's calibration (FanaLab, Pit House ...) applies first. The shim reads DirectInput's calibrated values and only overrides the Windows dead zone / saturation (0 / 100 %).
  * The calibration screen reads only the raw / min / max messages. So those messages make the shim read the controller, at most every 10 ms.
  * Tested end to end with a PS4 pad standing in for pedals. The test used the developer switch `GVRIOSHIM_TEST_PAD_AS_WHEEL=1` with `[Wheel] Gas = ry`, `Brake = rx`. The screen was shown, the presses were saved, the race used the range, and the next start skipped the screen.
* Steering: the cabinet's `Steering` slide is a power-on self-test of its force-feedback motor (`Gvr.AutoCalibrateSteering`, device 5). It runs only after an unscheduled Windows restart.
  * PC wheels calibrate their centre and ends themselves.
  * The shim answers `Gvr.AxisRawX` (msg 6, axis 0) with the PC wheel's steering, -127..127. So the operator Control Test's drawn wheel follows it.
  * Steering lock is set by `[Wheel] Rotation` / `DeviceRange` or the maker's software.
* Gamepads are never a wheel part or a button box. DirectInput types a PS4 pad as "other", so pads are also recognised by maker (Sony, Nintendo, Microsoft).
* Force feedback: `arcade` uses Steering.dll, which only opens devices that report as a driving device. For any other force-feedback base, the shim's own spring + damper take over.

The log lists every controller, then one `controls:` line. That line is repeated whenever something is learned:
`controls: wheel '...' | pedals '...' (learned) | shifter '...' (sequential) | buttons '...' | force feedback: ours`.

2026-10-12: on a Switch Pro pad, the operator menu is on Minus. `latch_switch` swaps the Minus and Plus positions in the front end only, so Minus reports as Options. In the race, Plus stays Start.
