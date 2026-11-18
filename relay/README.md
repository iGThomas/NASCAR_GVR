# Relay server

Lets players race each other over the internet without opening ports on their routers. Each
player's launcher connects outward to this server. The host opens a room, and the others join it
by name. The server passes the race traffic between them and stores nothing.

One Python file, standard library only. It runs anywhere with Python 3.8+ or Docker.

## Run with Docker

1. Copy this folder to the server.
2. In that folder, run:

```
docker compose up -d
docker logs -f nascar-relay
```

3. Open UDP port 22600 in the server's firewall, for example `sudo ufw allow 22600/udp`.

## Run without Docker

```
python3 nascar_relay.py
```

## Settings

Environment variables, set in `docker-compose.yml`:

| Variable | Default | Meaning |
|---|---|---|
| `RELAY_PORT` | 22600 | UDP port |
| `RELAY_MAX_ROOMS` | 100 | Rooms open at once |
| `RELAY_RATE_KBPS` | 2048 | Traffic cap per player, in KB/s. The host sends about 50 KB/s per other player. |

## Players

Each player sets these in `nascar_settings.ini`, section `[Multiplayer]`:

* `Relay`: this server's name and port, for example `relay.example.com:22600`.
* `Room`: a name everyone types the same.
* `Mode`: `host` for one player, `join` for the others.

See `docs/MULTIPLAYER.md`.
