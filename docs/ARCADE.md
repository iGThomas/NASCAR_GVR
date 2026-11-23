# Building an arcade cabinet

Both games start in Free Play. Coin mode makes them take coins like a real arcade machine, without a GlobalVR I/O board.

Any switch that reaches Windows as a key or a USB button can be a coin slot. Examples: a coin mechanism wired to a keyboard encoder or to a USB button board.

Home players do not need this page.

## 1. Wire the coin slots

A coin mechanism has a switch that closes for a moment when a coin drops. Wire that switch to a free input on your board, like any arcade button.

| Your board | What it sends | Example setting |
|---|---|---|
| Keyboard encoder | A key | `Coin1 = 5` |
| USB button board | A game controller button | `Coin1 = joy1:3` |

To find a USB button's number:

1. Press `Windows` + `R`, type `joy.cpl` and press Enter.
2. Select your board and click **Properties**.
3. Trigger the switch and note which button number lights up.

`joy1` is the first game controller in Windows. If a pad or wheel is also connected, your board may be `joy2` or higher.

## 2. Turn on coin mode

1. Close the game.
2. Open `nascar_settings.ini` in the install folder.
3. Find the `[Coin]` section and set `Enabled = true`.
4. Change the inputs if your board uses other ones.
5. Save the file.

```ini
[Coin]
Enabled = true
Coin1 = 5
Coin2 = 6
Service = 7
Debounce = 30
```

| Setting | What it does |
|---|---|
| `Enabled` | `true` turns coin mode on |
| `Coin1`, `Coin2` | The two coin slots |
| `Service` | Adds one free credit for the operator to give |
| `Debounce` | Milliseconds a switch must stay released before the next coin counts. Raise it if one coin counts twice. |

Each input takes one of these:

- A letter or number key, such as `5` or `c`.
- A function key, `f1` to `f24`.
- A named key: `lctrl`, `rctrl`, `lshift`, `rshift`, `lalt`, `ralt`, `space`, `enter`, `tab`, `insert`, `delete`, `home`, `end`, `pageup`, `pagedown`, `up`, `down`, `left`, `right`.
- A USB button, such as `joy1:3`.
- `none` to turn the input off.

Do not pick a key the game already uses: `D`, `G`, `R`, `C`, `S`, `L`, `V`, `M`, `J`, `O`, `T`, `Y`, `U`, `I`.

## 3. Turn off Free Play

Free Play is set in the game's operator menu.

| Button | Keyboard | Xbox | PlayStation |
|---|---|---|---|
| Open the operator menu | `O` | Menu | Options |
| Up (View) | `V` | D-pad up | D-pad up |
| Down (Music) | `M` | D-pad down | D-pad down |
| Select (Start) | `S` | A | Cross |
| Back (Look Back) | `L` | B | Circle |

1. Start the game and open the operator menu on the attract screen.
2. Go to **Coin Settings**:
   - NASCAR Racing: press Down five times.
   - Team Racing: press Back, then tap Up five times quickly.
3. Press Select to open **Configuration**.
4. On **Free Play**, press Select, choose **Disable** with Up, and press Select. NASCAR Racing spells it **Disabled**.
5. Leave the menu:
   - NASCAR Racing: press Back, press Down until **Resume Game** is highlighted, then press Select.
   - Team Racing: press Back twice, then press Select.

The bottom of the attract screen now shows **Insert Coins** and the amount paid. Insert coins and press Start to play.

To go back to Free Play, set it to **Enable** in the same place, then set `Enabled = false` in the settings file.

## Prices

By default one race costs four coins, which is $1.00.

1. In the operator menu, go to **Coin Settings** > **Pricing**.
2. Press Select on **Race** or **Race Continue**.
3. Change the number of credits with Up and Down.
4. Press Select to save.

The new price applies straight away. NASCAR Racing also has **Coins Per $1.00** under **Configuration**.

## Service credits

A service credit is a free coin given by the operator. The `Service` input adds one each time, once the switch is on:

- NASCAR Racing: **Coin Settings** > **Configuration** > **Service Credits Switch**.
- Team Racing: **Machine Settings** > **Settings** > **Service Credits Switch**.

The game spends service credits before paid coins.

## Earnings

NASCAR Racing calls the **Collection** menu **Collections**.

| Page | What it shows |
|---|---|
| **Collection** > **Current Earnings** | Money and service credits since the last collection |
| **Reset Bookkeeping**, on that page | Closes the period and starts a new one at 0.00 |
| **Collection** > **Last Collection** | The totals of the period you closed |
| **Coin Settings** > **Configuration** | Credits in the machine now, with reset buttons |
| **Diagnostics** > **Controls** | How many coins each slot has counted |

## Good to know

- The game takes the payment when the race ends. A race that crashes is not charged.
- Coins count only while the game is the window in front, so typing in another program adds nothing.
- Credits are saved in `Game\Save\nascar_coins.ini`. They survive restarts and updates.
- The first start in coin mode clears the credits once.

## Problems

| Problem | What to check |
|---|---|
| The screen still says Free Play | Free Play is still on in **Coin Settings** |
| A coin does nothing | `Enabled = true`, the input name, and that the game is the window in front |
| A USB button does nothing | Try `joy2` or `joy3`, and check the button number in `joy.cpl` |
| One coin counts twice | Raise `Debounce`, for example to `60` |
| The service button does nothing | **Service Credits Switch** is off |

Still stuck? Set `Log = true` under `[Debug]`, insert a coin, and send the `LOG` folder. See [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
