# Relay server

Lets players race each other over the internet without opening ports on their routers. Every
player's launcher connects outward to this server; the host opens a room, the others join it by
name, and the server passes the race traffic between them. It stores nothing.

One Python file, standard library only. Runs anywhere with Python 3.8+ or Docker.

## Run with Docker

Copy this folder to the server, then in it:

```
docker compose up -d
docker logs -f nascar-relay
```

Open UDP port 22600 in the server's firewall (for example `sudo ufw allow 22600/udp`).

## Run without Docker

```
python3 nascar_relay.py
```

## Settings

Environment variables (in `docker-compose.yml`):

| Variable | Default | Meaning |
|---|---|---|
| `RELAY_PORT` | 22600 | UDP port |
| `RELAY_MAX_ROOMS` | 100 | Rooms open at once |
| `RELAY_RATE_KBPS` | 2048 | Traffic cap per player, KB/s (the host sends about 50 KB/s per other player) |

## Players

In `nascar_settings.ini`, section `[Multiplayer]`: `Relay` = this server's name and port (for
example `relay.example.com:22600`), `Room` = a name everyone types the same, `Mode` = `host` for
one player and `join` for the others. See `docs/MULTIPLAYER.md`.
