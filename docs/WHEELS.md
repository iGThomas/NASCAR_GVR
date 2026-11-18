# Wheels, pedals and pads

Plug your devices in before you start the game. The game sets each one up by itself. You only need the settings below when something is wrong.

All settings are in `nascar_settings.ini`, in the install folder.

## Steering wheels

These wheels work with their own wheel and pedals:

- Logitech: Driving Force GT, G25, G27, G29, G920, G923
- Thrustmaster
- most other wheels that Windows lists as a driving device

Logitech G HUB is not needed, but it does no harm.

Force feedback is the arcade's own: centring, damping, vibration on grass and kerbs, and jolts in collisions. It also works on bases that Windows lists as a joystick.

## Separate pedals, shifters and button boxes

Each device can be on its own USB cable.

| Device | How the game finds it |
|---|---|
| Wheel base | Steering comes from the wheel. |
| Pedal box | By its name, or by the first pedal you press. |
| Shifter | By its name, or hold two gears for a few seconds each. The game remembers it after that. |
| Separate rim or button box | Its buttons work like the wheel's buttons. |

The game tells an H-pattern shifter from a sequential one by how you use it. Choose **Manual** on the transmission screen to use it.

A base that another program holds for force feedback is still read for steering.

## Pedal calibration

The first time the game sees your pedals, it shows "Calibrate the accelerator and brake". Press each pedal all the way down once.

The result is saved in `nascar_calibration.ini` and used in every race. So load-cell brakes and pedals limited in their own software still reach 100 %.

To calibrate again:

1. Press `O` to open the operator menu.
2. Go to **Control Test**.
3. Hold **Start + Music**.
4. Press each pedal all the way down once.

The same screen shows the steering wheel following yours. That is a quick check that the wheel works.

Calibration in the maker's own software, such as Fanatec, Moza or Thrustmaster, still applies.

## When a device is wrong

1. Set `Log = true` under `[Debug]` and start a race.
2. Open the `LOG` folder. The log lists every controller, its pedal layout and every button number.
3. The `controls:` line shows what each device became.

Then fix the wrong guess with one line under `[Wheel]`:

| Setting | Use it when |
|---|---|
| `SwapPedals = true` | Gas and brake are the wrong way round. |
| `Device` | The wrong device steers. Enter part of the wheel's name. |
| `PedalDevice` | The wrong device is used for the pedals. |
| `ShifterDevice` | The wrong device is used as the shifter. `none` means no shifter. |
| `ShifterMode` | The shifter type is guessed wrong. Use `h` or `sequential`. |
| `ButtonBox1` | A button box is not found. |
| `Rotation` | You want a different steering range, in degrees. |

The names come from the log's `controllers:` lines.

## Gamepads

Xbox and PlayStation pads work in the menus and the race. The button map is under `[Controller]` for the race and `[Frontend]` for the menus.

### Switch Pro Controller

It works over USB or Bluetooth. The buttons follow the same positions as on the other pads, so A and B are swapped compared with Switch games.

ZL and ZR are on or off, not gradual. For gradual gas and brake, set `Pedals = rightstick` under `[Controller]`. Then right stick up is gas and down is brake.

Steam can also show the pad to the game as an Xbox controller.

## All controls

Every control and how to remap it: [findings/NASCAR_CONTROLS.md](findings/NASCAR_CONTROLS.md).
