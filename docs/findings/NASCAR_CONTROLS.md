# NASCAR Team Racing GVR — every control, as stored

Extracted 2026-10-03 from the installed configs (`D:\Games\NASCAR`), corrected 2026-10-04
(`NASCAR_FINDINGS.md` §14). Two layers:
* **GvrIO** carries everything the cabinet has — the wheel and pedals (analog axes) *and* the
  buttons and shifter (`GvrIO.xml`) — to both the shell (Anark front end) and the race.
* The race's own **`.CTL`** file only adds **keyboard** keys (clutch, pit menu, replay, …). Its
  "controller 1" lines are never read: this build opens no DirectInput joystick.

Xbox / PlayStation pads plug into GvrIO, like the cabinet: section 4. Rebinding: section 3.

## 1. Shell / cabinet buttons — `Game\config\GvrIO.xml`

On a PC the keyboard column is what works. Board numbers are the cabinet's GvrIO-mini inputs
(`Steering` = the older wheel board numbering).

| Function | Keyboard | GvrIO-mini input | Steering-board input | Script event |
|---|---|---|---|---|
| Start | `S` | 0 | 20 | `onGvrStartPressed` |
| Look back | `L` | 1 | 9 | `onGvrLookBackPressed` |
| Look back 2 | — | 20 | — | `onGvrLookBack2Pressed` |
| Music | `M` | 2 | 8 | `onGvrMusicPressed` |
| View | `V` | 3 | 0 | `onGvrViewPressed` |
| Volume up | — | 10 | — | `onGvrVolumeUpPressed` |
| Volume down | — | 11 | — | `onGvrVolumeDownPressed` |
| Operator (service menu) | `O` | 12 | — | `onGvrOperatorPressed` |
| Service | — | 13 | — | `onGvrServicePressed` |
| Coin 1 / Coin 2 | — | 8 / 9 | — | `onGvrCoin1Pressed` / `onGvrCoin2Pressed` |
| Shifter 1–4 | `T` `Y` `U` `I` | 16–19 | — | `onGvrShifter1..4Pressed` |
| Menu forward / back — **race: gas / brake** | `R` / `C` | — | — | `onGvrForwardPressed` / `onGvrBackPressed` |
| Menu left / right — **race: steer** | `D` / `G` | — | — | `onGvrLeftPressed` / `onGvrRightPressed` |
| Quit | `Q` | — | — | `onGvrQuitPressed` |
| Motion disable | `A` | — | — | `onGvrMotonDisablePressed` (sic) |
| Test key | `X` | — | — | `onGvrXKeyPressed` |

Selection on the cabinet is the steering wheel (`onSelectionChanged`, GvrIO axis X). In the
motion-error slide, Start + Music continues without motion.

**Shell screens on a PC (2026-10-04):**

| Screen | Keys |
|---|---|
| Attract ("Press Start") | `S` start · `O` operator menu |
| Car select ("Start Your Engines") / transmission / track | `←` or `D` = steer left, `→` or `G` = steer right (the shell's GvrIO shim feeds these to `Gvr.AxisX` when no wheel is attached), `S` = select (or the gas pedal on a wheel) |
| Operator menu (legend on screen) | `V` View = up · `M` Music = down · `S` Start = select · `L` Look Back = cancel/back |

The steering keys only act while the shell window has focus. A real wheel/pedal device always
takes priority over the keyboard.

## 2. Race controls — `.CTL` files

`(device, code)`: device `0` = keyboard with a DirectInput scan code; device `1` = the first game
controller (wheel or pad). `(0, 89)` means unbound.

**Live set** — `Game\Save\GvrSinglePlayer\GvrSinglePlayer.CTL` (what the cabinet uses):

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
| Horn | `J` — bound by this install (`Control - Horn="(0, 36)"`, OEM unbound). Not H: H cycles the HUD (normal / hidden / debug overlay) |
| Chase (Swingman) camera | numpad `4` / `6` turn, `8` / `2` up / down (engine keys, not in the file) |
| Popups / standings | `F1`–`F5`, `Space`, `I` / `K` (race info, standings, lap times, tires, pit stop) |

**The controller lines are dead on this build** (2026-10-04): the race creates only a DirectInput
*keyboard*, so `(1, …)` bindings are never read. It drives from GvrIO instead — the cabinet wheel
and pedals (or a pad, section 4) as analog axes, or the `GvrIO.xml` keys **`R` gas, `C` brake,
`D` / `G` steer** (digital ramps). Gear-select buttons (neutral, reverse, 1st–7th) are unbound; a
manual gearbox uses the cabinet's 4-position shifter (`T` `Y` `U` `I` = gears 1–4, held).

**Stock keyboard preset** — `Game\Options\Basic Settings\Keyboard.CTL`: accelerate `↑`, brake `↓`,
steer `←`/`→`, shift up `A`, shift down `Z`, clutch `Q`, pit menu `I` `K` `L` `J`, plus the same
`O` `[` `]` `V` `C` `B` `R` `P` `W` `Tab` as above, push-to-talk `` ` ``, realtime chat `T`, quick chat
`1`–`0`. The other presets in that folder are `Default Wheel.ctl`, `Logitech - MOMO.ctl`,
`Microsoft - Wheels - R1.ctl` / `R2.ctl` and `Saitek - R440.CTL`.

Keyboard tuning in the live set: steering rate `0.18`, throttle `0.50`, brake `0.50`, clutch
`0.25`; force feedback off; per-axis dead zone / sensitivity blocks follow as
`Axis [01, nn] ...` lines.

Decoder: `NASCAR\Tools\Decode-Ctl.py <file.CTL>` prints every binding with key names.

## 3. Changing a binding without breaking the cabinet (2026-10-04)

* **`GvrIO.xml` keeps keyboard and cabinet separate.** Every action has its own `<button num=…
  board=…>` line per board input and its own `<key char=…>` line, all naming the same
  `messageID`/`event`. Editing a `<key>` line never touches the cabinet input, and both work at
  once. One file serves both processes: `GvrIO_oem.dll` (the same build in `Game\` and
  `Shell\bin\`) reads `HKLM\SOFTWARE\Gvr\Plus\2.0\Cabinet\GameRoot` (here `D:\Games\NASCAR\Game\`)
  and loads `<GameRoot>\Config\GvrIO.xml`. `NASCAR_GVR.exe` registers for the `onGvr…ButtonDown/Up`
  events of View, LookBack, LookBack2, Music, Start, NOS, MotionDisable, Left, Right, Forward and
  Back, so the race also takes the cabinet buttons from this file.
* **The `.CTL` cannot break the wheel.** Each `Control - <name>="(device, code)"` holds one
  binding, but the race never reads its controller lines (section 2): the wheel, pedals and pad
  arrive through GvrIO. Rebinding a `.CTL` action to a key therefore only changes the keyboard.
  (Corrects the 2026-10-04 afternoon note, which assumed controller 1 was the wheel.)
* **Car-select keyboard steering is hardcoded in the shim**, not in a config file
  (`GvrIOShim.cpp`: `VK_LEFT`/`D` and `VK_RIGHT`/`G` via `GetAsyncKeyState`). It only runs while
  OEM GvrIO reports no usable pedal range (max < 40), so a connected wheel switches it off by itself.
* **Letters bound in both files:** `V` (View / Rear Look), `R` (Forward / Instant Replay) and `C`
  (Back / Look Left). `Q` and `O` are also in both, but the race does not listen for Quit or
  Operator. Check both files before giving a key a new job.

## 4. Xbox and PlayStation pads (2026-10-04)

Xbox pads through XInput, PS4 DualShock 4 through raw HID (USB or Bluetooth). Built into the `GvrIO.dll` shim (`src\GvrIOShim\GvrIOPad.cpp`):
the sticks and triggers are the cabinet's analog wheel and pedals, and each button "holds" the
cabinet key it stands for in GvrIO's own keyboard, so both programs treat it exactly like the
cabinet button. Map: `nascar_settings.ini` `[Controller]` (race) and `[Frontend]` (menus), as
`button = action` lines (e.g. `Square = shiftup`); `GVRIOSHIM_NO_PAD=1` turns it off. How it works:
`NASCAR_FINDINGS.md` §14.

| Pad (Xbox) | Race | Menus |
|---|---|---|
| Left stick | steering | car / track select (as the wheel) |
| R2 (RT) / L2 (LT) | gas / brake | gas = select, as the cabinet's pedal |
| Cross (A) | — | select (Start) |
| Circle (B) | horn (the engine's Horn control, as keyboard `J`) | back (Look Back) |
| Square (X) / Triangle (Y) | shift up / down (manual transmission, 4 gears) | — |
| L1 (LB) | look back | — |
| R1 (RB) | camera (View) | — |
| Options (Menu) | Start: put the car back on the track | operator menu |
| D-pad up / down | — / **quit: hold 1 s** | up / down (View / Music) |
| D-pad left / right | — / music | left / right (+ steer car and track select, + Auto / Manual) |
| Right stick | chase-camera turn (numpad 4/6/8/2) | — |

**Notes on the layout:**
* **Not on the pad:** the race has no e-brake or nitrous action (Circle is the horn), and no quit
  *prompt* — the quit is the cabinet's four-button abort, so the pad makes you hold D-pad down for
  a second. The menus use R2 as the gas pedal, not for name-entry accept / backspace.
* **Cabinet functions on the pad:** the horn, the right-stick chase camera, the 4-speed shifter
  (4 gears), Start = put the car back on the track, and the four-button abort. Cabinet-only buttons with no pad button: Volume
  up/down, Service, Coin, Motion disable, LookBack2.
* **Keyboard-only race extras** (the race's own `.CTL` keyboard, not reachable through GvrIO):
  clutch, pit menu, traction-control override, brake bias, look left/right, instant replay,
  pause, restart, vehicle labels, the F1–F5 popups. (The horn and the numpad camera are the
  two the pad does press there.)

Transmission select moves with GvrIO's own selection, which steps on a *press* of Left/Right:
on a keyboard that is **numpad 4 / 6** (the arrow keys' codes map to nothing in GvrIO); on
the pad, D-pad left/right or the stick pushed over half-way. Confirmed with a PS4 pad over
Bluetooth on 2026-10-04 (`NASCAR_FINDINGS.md` §14.6).

**2026-10-05:** removed `GvrIO.xml`'s `<key char="a" name="MotionDisable">` (OEM kept as `GvrIO.xml.oem`). Held, it shows the race's motion-seat HUD panel — on AZERTY it is the A key, which looked like a stray popup. Note that **the race rewrites `GvrSinglePlayer.CTL` when it exits**, so edit it only while no race is running.

**Horn (final, 2026-10-05):** keyboard **J** or pad Circle — the cabinet NOS input (`GvrIO.xml` `name="NOS"`), stopped on release by the GvrIO shim. The `.CTL` `Control - Horn` does nothing in this build and is unbound again. (Rows above that say the horn is the `.CTL` Horn on H/J are superseded.)

### 4.x Nintendo Switch Pro Controller (2026-10-11, untested on hardware)

`GvrIOPad.cpp` reads it through the same raw-HID path as the DS4 (VID `057E`, PID `2009`; the
first HID pad found wins, XInput still comes first).

* **Wake-up** (`switch_init`, discovery thread, before the handle goes to the game threads): the
  pad is reopened with write access. USB only: output report `80 02`, `80 03`, `80 02`, `80 04`
  (handshake, 3 Mbit, handshake, USB-only — SDL's order). Both links: subcommand `03 30` (full
  report mode) and `30 01` (player LED 1 = visible proof it worked). Subcommand = report `01`,
  4-bit counter, 8 bytes neutral rumble `00 01 40 40 00 01 40 40`, id, arg. Bluetooth is detected
  by the HID service GUID `00001124` in the device path. Without write access a Bluetooth pad
  still works in its simple mode.
* **Reports** (`latch_switch`): `0x30` / `0x21` — byte 3 `Y X B A SR SL R ZR`, 4
  `- + RS LS Home Capture`, 5 `down up right left SR SL L ZL`, 6–8 / 9–11 = left / right stick as
  two 12-bit values (centre ~2048, up = larger; dead zone 200, full 1500, no factory calibration
  read). `0x3F` simple Bluetooth report — byte 1 `B A Y X L R ZL ZR`, 2 `- + LS RS Home Capture`,
  3 hat, 4–11 = 16-bit LX LY RX RY.
* **Mapping by position** (like Xbox): B = Cross, A = Circle, Y = Square, X = Triangle, L/R/ZL/ZR =
  L1/R1/L2/R2, −/+ = Share/Options, Home = PS, Capture = `capture` (unbound). ini names `l r zl zr
  minus plus home capture` are accepted; `a b x y` stay the Xbox *positions*.
* **ZL / ZR are digital** → gas/brake 0 or 255. `[Controller] Pedals = rightstick` (any pad) adds
  right-stick up/down as gas/brake (max of stick and trigger) and stops the right stick turning
  the Swingman camera vertically.
* Verified without hardware: a decoder harness (synthetic `0x30` / `0x21` / `0x3F` reports + a DS4
  regression) passes, and shell + race run with the new DLL. **Not yet verified with a real pad on
  USB or Bluetooth**: on a working pad the log shows `pad: Switch Pro Controller over … - full
  report mode requested` and the player-1 LED lights.

### 4.y Steering wheels and force feedback (2026-10-11, untested on hardware)

**The OEM already supports a PC force-feedback wheel.** `Steering.dll` (the GvrIO plug-in that
`GvrIO.xml`'s `<controller type="Steering">` loads, next to both programs) has a DirectInput back
end: it enumerates attached force-feedback devices of type `DI8DEVTYPE_DRIVING` (0x16), opens one
exclusive + background, turns autocentre off and creates Spring, Damper, two Sine and a
ConstantForce effect; only without such a device does it fall back to its HID protocol for the
cabinet's Happ FFB board. It reads only the wheel's **X axis** (`GetDeviceState(0x110)` at
`0x10003090`, `lX` as a word); on the cabinet the pedals come from the I/O board.

**What the race sends** (all to GvrIO device 5; `0x45c130`/`0x45c500` at setup, `0x45c650` per
frame, only when a value changed): `0x1d` spring (magnitude, centre offset), `0x35` damper,
`0x19` / `0x1b` sine A / B (force, period). Spring and damper ramp in over `FF Spring Ramp Time`
(3 s). The sines are per-tyre surface (grass `0xF`, rumble strip `0x12`, road) lerped by speed
(`min(1, |v| / FF Speed Throttle 50)`). Collisions (`0x4d5ab6`, `0x4d5cfc`) set the spring centre
to +/- Collision Offset with Collision Force for that frame. Steering.dll maps them to DirectInput:
spring / damper `DICONDITION` coefficients 10000, gain = value x 100, spring offset = centre x 100;
sines magnitude 10000, offset -5000, gain = force x 100. There is no physics steering torque.
Arcade defaults (`GvrSinglePlayer.PLR` profile "FF Handling", operator "Steering Feedback
Strength" Normal / Light = `-handling 0/1`): spring 40 / 25, damper 40 / 25, rumble 70-75 /
50-55, grass 21-32 / 11-21, collision cars 25/15 (offset/force), walls 35/25. The EA `.CTL` FFB
block is dead code (no DirectInput effect GUIDs in the exe).

**The shim's part** (`src\GvrIOShim\GvrIOWheel.cpp`, `[Wheel]` in `nascar_settings.ini`):
* Input. The shim already answers msg 5 when no I/O-board pedals exist, so the wheel feeds the
  same `PadState` as the pads: steering -127..127 (optional `Rotation` / `DeviceRange` scaling),
  gas / brake 0..255 from separate axes (default `y` / `rz`) or one combined axis; each pedal's
  rest value is learned from the first reading, so either direction works. Buttons map to pad
  positions (`ButtonN = cross`...; DFGT guesses built in, every press logged with its number); the
  hat is the D-pad. The wheel is found on the pad discovery thread (first `DI8DEVTYPE_DRIVING`, or
  `[Wheel] Device=` name match) with the shim's own `IDirectInput8A`, read **non-exclusively**, so it
  never conflicts with Steering.dll's exclusive hold.
* Force feedback. `ForceFeedback = arcade` (default) leaves it to Steering.dll - the exact cabinet
  effects. `shim` = the shim's own spring + damper (arcade Normal 40 / 40, `ForceSpring` / `ForceDamper`
  / `ForceGain`), exclusive + foreground on the process's foreground window, for a wheel Steering.dll does
  not pick up. `off` = no shim effects.
* Trap fixed with it: GvrIOPad's shared `GetDeviceState` hook used to treat every read of >= 256
  bytes as a keyboard; Steering.dll's 272-byte `DIJOYSTATE2` read would have received the pad's
  right-stick camera keys as button presses. Now exactly 256 bytes only, and the shim's own wheel is skipped.
* Known risks, not yet verified on hardware: (1) the shell and the race BOTH load Steering.dll; if the shell
  keeps the wheel exclusively while the race runs, the race may get no force feedback (the race's
  acquire would fail), so FFB working in the menus does not prove it works in a race; (2) the DFGT must
  run in native mode (Logitech G HUB / Gaming Software) to enumerate as a driving device with
  separate pedals; (3) Steering.dll's shell self-test can write `ForceFeedbackBroken` to the
  NASCAR registry key.

**2026-10-12 - first real-wheel test (Logitech G29 + Driving Force Shifter, v1.0.8):** steering,
the arcade force feedback (via Steering.dll) and the paddle shifters work; **pedals and the H-shifter
did not.** Pedals: with a wheel attached, Steering.dll reports a calibrated gas range, so the shim's
`real_pedals` check passed the OEM's axis values through - and the OEM gas/brake come from the absent
I/O board (0). Fix (1.0.9): `wheel_present()` - while the shim's wheel is open its axes always win
(GvrIOShim.cpp msg 5, race and shell). H-shifter: the shifter's gears are wheel buttons 12..17
(gear 1..6, 18 = reverse) on G29/G920/G923; new pad actions `gear1`..`gear4` select the arcade's
4-position shifter gear directly (5th/6th = 4th), defaulted for those wheels, `[Wheel] ButtonN =
gearN` for others. The log now names each wheel axis the first time it moves
(`wheel: axis 'rz' moves ...`), so the log shows which axis is gas / brake / clutch.
Follow-up (same day, v1.0.8): the pedals DID work in the menus (driver selection) and stopped
once the race (NASCAR_GVR.exe) loaded - each program makes its own `real_pedals` decision, and only the
race's OEM Steering.dll reported a calibrated range. The 1.0.9 `wheel_present()` override covers both.
Paddles "not working" in the race = automatic transmission: sequential (paddles) and H-shifter gears
only act in **manual**, chosen on the shell's transmission screen.

**2026-10-26 - several devices, set up by themselves.** Direct-drive rigs split the controls over
several USB devices (base, pedal box, shifter, rim, button box). The shim reads them together and
decides each role itself; every `[Wheel]` device setting is only an override:
* Wheel: `Device`, else a force-feedback driving device, else a force-feedback device that is not a
  gamepad (a base listed as a joystick), else a driving device - never a pedal box or shifter.
* Pedals: `PedalDevice`, else a name with "pedal" / "LCM", else learned: for a wheel that is not a
  known model, every other device is watched and the first whose axis rests at an end of its range
  and then travels more than half of it (a pedal; stick and wheel axes rest in the middle; at least
  two such axes) becomes the pedal box. Its pedal axes in order are gas, brake (clutch ignored);
  Rz + Y is read Logitech-style (gas Rz). Known models (Logitech, Thrustmaster presets) keep their
  own pedals, so a USB handbrake never takes over.
* Shifter: `ShifterDevice`, else "shift" / "TH8" / "TSS". `ShifterMode = auto` learns the type: a
  button held over 0.4 s is an H-pattern gear (buttons 0..5 = gears 1..4, 4, 4), two short taps
  without such a hold make it sequential (0 / 1 = square / triangle = shift up / down). Until then a
  press waits up to 0.4 s; a tap is passed on as a 150 ms press.
* Shifter without "shift" in its name (a generic USB shifter): it starts as a button slot; a device
  whose axes never move and which holds one button over 2 s, later another one (two gears - rim and
  box buttons are tapped, a toggle switch is one button) becomes the shifter, H-pattern. Buttons
  already down at the first reading (a mode switch) are ignored until released once. The result is
  saved in `nascar_calibration.ini` `[Shifter]` (VID:PID), so the shell and every race pick it at
  once. Named button boxes never become the shifter; `ShifterDevice = none` turns shifters off.
* Buttons: every other controller that is not a gamepad (a separate rim, a button box) adds its
  buttons and hat through the wheel's `ButtonN` map; `ButtonBox1..3` (+ `ButtonBoxKButtonN`) name them.
* Access: a base that Windows lists as a joystick with force feedback (HID PID direct-drive bases,
  e.g. MOZA, USB VID 346E) gets the shim's own force feedback, which needs exclusive foreground
  access. The menus and the race are two programs; when the exclusive `Acquire` is refused (the
  other program or the maker's software still holds the base) the wheel is read shared, without
  the shim's force feedback in that program, until its window changes. Before, a refused
  `Acquire` left the wheel unread: steering 0 while separate pedals (always read shared) worked.
* Discovery runs only on the pad thread. The front end's start-up pedal check waits for its first
  pass at most 3 s; menu selection (transmission screen) moves from the OEM to the shim as soon as
  a PC wheel or pedal box is found, so a slow first scan no longer leaves the transmission screen
  on the cabinet-wheel path.
* Pedal calibration is the cabinet's own. The I/O board keeps per pedal a raw position (GvrIO
  msg 6 = `Gvr.AxisRawY/Z`) and a min / max (`0x39` / `0x3A` = `Gvr.AxisMin/MaxY/Z`) that grows as
  the pedal moves; `Gvr.ResetCalibration = true` (msg `0x53`) clears it; `Gvr.AxisY/Z` (msg 5) is the
  calibrated value. `Shell.am` shows `PedalCalibrate` ("Calibrate the accelerator and brake") when
  max - min < 40, and the operator Control Test resets it on Start + Music. For PC pedals the shim
  plays the board (property table of `ZeusIOPlugIn.dll`, identical message ids in 1.1 and 2.0.2):
  raw = travel from the released position, 0..255 of the axis; min / max saved in
  `<install>\nascar_calibration.ini` for the pedal device (`[Pedals] Device`, `GasMin/Max`,
  `BrakeMin/Max`), so the shell and every race share it and new pedals start uncalibrated (the
  shell asks once). Until calibrated, 100 % = the deepest point reached in that race (at least half
  the axis). Pads and combined Logitech pedals keep the former fixed 0..255 answer (no screen).
  The maker's calibration (FanaLab, Pit House ...) applies first - the shim reads DirectInput's
  calibrated values and only overrides the Windows dead zone / saturation (0 / 100 %).
  The calibration screen reads only the raw / min / max messages, so those make the shim read the
  controller (at most every 10 ms). Tested end to end with a PS4 pad standing in for pedals
  (developer switch `GVRIOSHIM_TEST_PAD_AS_WHEEL=1` with `[Wheel] Gas = ry`, `Brake = rx`): screen
  shown, presses saved, the race used the range, the next start skipped the screen.
* Steering: the cabinet's `Steering` slide is a power-on self-test of its force-feedback motor
  (`Gvr.AutoCalibrateSteering`, device 5; it runs only after an unscheduled Windows restart) - PC
  wheels calibrate their centre and ends themselves. The shim answers `Gvr.AxisRawX` (msg 6, axis 0)
  with the PC wheel's steering (-127..127), so the operator Control Test's drawn wheel follows it.
  Steering lock is `[Wheel] Rotation` / `DeviceRange` or the maker's software.
* Gamepads are never a wheel part or a button box: DirectInput types a PS4 pad as "other", so pads
  are also recognised by maker (Sony, Nintendo, Microsoft).
* Force feedback: `arcade` uses Steering.dll, which only opens devices that report as a driving
  device; for any other force-feedback base the shim's own spring + damper take over.
The log lists every controller, then one `controls:` line (repeated whenever something is learned):
`controls: wheel '...' | pedals '...' (learned) | shifter '...' (sequential) | buttons '...' | force feedback: ours`.

**2026-10-12:** on a Switch Pro pad the operator menu is on **Minus**. `latch_switch` swaps the Minus/Plus positions in the front end only (Minus reports as Options); in the race Plus stays Start.
