# Multiplayer

Race other players over the internet or on the same network. The game uses its own network code, as on linked arcade cabinets. There are two ways to play:

| Way | What players see | Games |
|---|---|---|
| Arcade lobby, `Mode = lobby` | The original arcade flow. The attract shows "Join now! Play head-to-head". Players choose a driver, meet in the Lobby, and race together after a countdown. | NASCAR Racing |
| Straight into a race, `Mode = host` or `join` | A small waiting window, then the race. The arcade menus are skipped. | Both games |

## Arcade lobby (NASCAR Racing)

It is on by default, so there is nothing to set up. The settings are under `[Multiplayer]` in `nascar_settings.ini`:

| Setting | Default | What it does |
|---|---|---|
| `Mode` | `lobby` | The menus link with other players online. `off` means never online. |
| `Relay` | `nascar.furioussteel.net:22600` | The server that connects the players. |
| `Room` | empty | Empty is the public lobby, open to anyone online. A name such as `fridaynight` makes a private lobby. Give your friends the same name. |
| `PlayerName` | empty | Your name for the others. Empty shows "Driver 1", "Driver 2" and so on. |

Everyone starts the game as usual and chooses a driver. Alone, the race starts as always.

With other players online:

1. The first player to choose a track sets it for everyone.
2. All players meet in the Lobby.
3. The race starts when the countdown ends. Up to 10 players can race.

The cabinet that chose first runs the race for the others.

Good to know:

- Start the game on every PC first. A player who chooses a track before the others are connected races alone, as on the arcade.
- When everyone who is choosing has reached the Lobby, the race starts after 15 seconds. A player who presses Start in that time is waited for.
- When a player leaves a race, the others see "Connection lost" and return to the menus.

## Straight into a race

Everyone sets these under `[Multiplayer]` in `nascar_settings.ini`:

| Setting | Host, one player | Everyone else |
|---|---|---|
| `Mode` | `host` | `join` |
| `Relay` | The relay server. The default is `nascar.furioussteel.net:22600`. | The same |
| `Room` | Empty for the public room, or a name you pick | The same as the host |
| `Players` | How many players to wait for, including you: 2 to 10. Not used in the lobby. | Not used |

Then everyone starts the game as usual. A small window shows who is in.

The race starts when all `Players` are in. The host can also press **Start now** to race with the players already there.

Track, series and laps come from the host's `[Race]` settings. Each player drives their own `[Race] Car`.

### On the same network, without a relay

1. Everyone leaves `Relay` empty.
2. The host sets `Mode = host` and `Players`.
3. The others set `Mode = join`, and `HostAddress` to the host PC's IP address, for example `192.168.1.20`. The command `ipconfig` shows it.
4. Start the game on the host first.

Windows Firewall asks the host PC once whether to allow the game. Allow it on the private network.

## Both ways

- `PlayerName`, up to 15 letters, is your name on the Lobby card and on your car in the race. Every player sees it. Empty shows "Driver 1", "Driver 2", as on the arcade.
- Nothing needs to be set up on the router. Every PC only connects out to the relay server.
- Room names use letters, numbers, `-` and `_`, up to 15 characters. Upper and lower case count as the same.
- A room holds up to 10 players in the lobby. When the public one is full, use a private room.

## Running a relay server

See [relay/README.md](../relay/README.md). It is one small Python program, or `docker compose up -d`.

## Good to know

- The race uses UDP port 22544 on the host on the same network, and the relay's port over the internet.
- With `Log = true` under `[Debug]`, the game writes `LOG\gvrlinking-<number>.log`. It lists the cabinets in the room and the messages sent and received. The `multiplayer:` lines in `LOG\nascarlaunch.log` show the rest.
