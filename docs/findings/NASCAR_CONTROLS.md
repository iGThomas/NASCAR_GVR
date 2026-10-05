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

Same pads and layout as NFS Underground (`GvrInputEmu`): Xbox through XInput, PS4 DualShock 4
through raw HID (USB or Bluetooth). Built into the `GvrIO.dll` shim (`src\GvrIOShim\GvrIOPad.cpp`):
the sticks and triggers are the cabinet's analog wheel and pedals, and each button "holds" the
cabinet key it stands for in GvrIO's own keyboard, so both programs treat it exactly like the
cabinet button. Map: `nascar_settings.ini` `[Controller]` (race) and `[Frontend]` (menus), with
NFSU's syntax and button names; `GVRIOSHIM_NO_PAD=1` turns it off. How it works:
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

**Compared with NFSU:**
* **Not in NASCAR:** e-brake (NFSU Cross), nitrous (NFSU Circle — Circle is the horn here), the
  quit *prompt* (NASCAR has none — the quit is the cabinet's four-button abort, so the pad makes
  you hold D-pad down for a second), the card button (NFSU R3), and the name-entry
  accept/backspace on R2/L2 (NASCAR's menus use R2 as the gas pedal instead).
* **New in NASCAR:** the horn, the right-stick chase camera, the 4-speed shifter (4 gears, not
  NFSU's 6), Start = put the car back on the track, and the four-button abort. Cabinet-only buttons with no pad button: Volume
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
