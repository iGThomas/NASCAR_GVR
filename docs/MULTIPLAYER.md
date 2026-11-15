# Multiplayer (beta)

Linked races between PCs, over the internet or on the same network, using the game's own network
code as on linked arcade cabinets. Two ways to play:

| Way | What players see | Games |
|---|---|---|
| **Arcade lobby** (`Mode=lobby`) | The original arcade flow: "Join now! Play head-to-head" in the attract, choose a driver, meet in the Lobby with a countdown, race together | NASCAR Racing |
| **Straight into a race** (`Mode=host` / `join`) | A small waiting window, then the race; the arcade menus are skipped | Both |

## Arcade lobby (NASCAR Racing)

On by default - nothing to set up. In `nascar_settings.ini`, section `[Multiplayer]`:

| Setting | Default | Meaning |
|---|---|---|
| `Mode` | `lobby` | the menus link with other players online; `off` = never online |
| `Relay` | `nascar.furioussteel.net:22600` | the server that connects the players |
| `Room` | empty | empty = the public lobby (anyone online); a name = a private lobby, e.g. `fridaynight` - give your friends the same name |
| `PlayerName` | empty | your name for the others; empty = "Driver 1", "Driver 2", ... |

Everyone starts the game as usual and chooses a driver. Alone, the race starts as always. The first player to choose a track
sets it for everyone; all players meet in the Lobby, and the race starts when the countdown ends.
Up to 10 players. The cabinet that chose first runs the race for the others.

- Start the game on every PC first: a player who chooses a track before the others are connected
  races alone, as on the arcade.
- When everyone who is choosing has reached the Lobby, the race starts after 15 seconds. A player who
  presses Start in that time is waited for.
- When a player leaves a race, the others see "connection lost" and return to the menus.

## Straight into a race

Everyone sets, in `nascar_settings.ini`, section `[Multiplayer]`:

| Setting | Host (one player) | Everyone else |
|---|---|---|
| `Mode` | `host` | `join` |
| `Relay` | the relay server (default `nascar.furioussteel.net:22600`) | the same |
| `Room` | empty (the public room) or a name you pick | the same as the host |
| `Players` | how many to wait for, including you, 2 to 10 (not used in the lobby) | (not used) |

Then everyone starts the game as usual. A small window shows who is in. The race starts when all
`Players` are in; the host can also press **Start now** to race with the players already there.

- Track, series and laps are the host's `[Race]` values. Each player drives their own `[Race] Car`.

### On the same network, without a relay

Leave `Relay` empty. The host sets `Mode=host` and `Players`; the others set `Mode=join` and
`HostAddress` to the host PC's IP address (for example `192.168.1.20`; shown by `ipconfig`). Start
the host first. Windows Firewall asks the host PC once whether to allow the game: allow it on the
private network.

## Both ways

- `PlayerName` (up to 15 letters): your name on the Lobby card (arcade lobby) and on your car in the
  race, for every player. Empty = "Driver 1", "Driver 2", as on the arcade.
- Nothing to set up on the router: every PC only connects out to the relay server.
- Room names: letters, numbers, `-` and `_`, up to 15 characters, not case-sensitive. A room holds up
  to 10 players in the lobby; when the public one is full, use a private room.

## Running a relay server

See `relay/README.md`: one small Python program, or `docker compose up -d`.

## Good to know

- The race uses UDP port 22544 on the host (same network) and the relay's port (internet).
- `[Debug] Log=true` writes `LOG\gvrlinking-<number>.log` (the lobby link: cabinets in the room,
  messages sent and received) and the `multiplayer:` lines in `LOG\nascarlaunch.log`.
