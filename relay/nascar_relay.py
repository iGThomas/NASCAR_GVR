#!/usr/bin/env python3
"""Relay server for NASCAR GVR linked races (internet play without router setup).

Every player's launcher talks only outward to this server over UDP. The host opens a room,
the other players join it by name, and the server passes the race packets between the
host and each player. Python 3.8+, standard library only.

Settings (environment variables):
  RELAY_PORT        UDP port (default 22600)
  RELAY_MAX_ROOMS   rooms open at once (default 100)
  RELAY_RATE_KBPS   traffic cap per player in KB/s (default 2048; a host with 7 players sends about 360)

Protocol: every message starts with b"NGR1" and a type byte.
  1 HOST_HELLO  host -> relay   ver, room[16], info[96]           (every 2 s)
  7 STATUS      relay -> host   started, players joined, peer mask (2 bytes)
  2 JOIN_HELLO  join -> relay   ver, room[16], padded to 128 bytes (every 2 s)
  8 WELCOME     relay -> join   peer, started, players in race, info[96]
  3 START       host -> relay   the room closes to new players
  4 DATA        join -> relay   payload    -> host gets peer + payload
                host -> relay   peer + payload -> that player gets payload
  5 ERROR       relay -> any    code, text
  6 BYE         any -> relay    leaving
Arcade lobby (the menus' cabinet link; every member is equal, up to 10 per room):
  9 LINK_HELLO  member -> relay ver, room[16], padded to 128 bytes (every second)
 10 LINK_STATUS relay -> member own member id, member mask (2 bytes)
 11 LINK_BCAST  member -> relay payload  -> every other member gets src + payload
 12 LINK_DATA   member -> relay dst, dir, payload -> member dst gets src, dir, payload
Replies to a hello are never larger than the hello. Room traffic only goes to addresses that
joined the room themselves and stops when they go quiet.
"""
import os
import re
import socket
import sys
import time

MAGIC = b"NGR1"
VERSION = 1
ROOM_LEN, INFO_LEN, JOIN_HELLO_LEN = 16, 96, 128
HOST_HELLO, JOIN_HELLO, START, DATA, ERROR, BYE, STATUS, WELCOME = 1, 2, 3, 4, 5, 6, 7, 8
E_ROOM_TAKEN, E_NO_ROOM, E_STARTED, E_FULL, E_VERSION, E_SERVER_FULL, E_HOST_LEFT = 1, 2, 3, 4, 5, 6, 7
LINK_HELLO, LINK_STATUS, LINK_BCAST, LINK_DATA = 9, 10, 11, 12
MAX_PEERS = 9            # players besides the host (10 in all, as the arcade link)
IDLE_SECONDS = 20        # a host or player silent this long is gone
LINK_MEMBERS = 10        # the menus' link knows 10 cabinets
LINK_IDLE = 6            # a lobby member silent this long has left (it says hello every second)
MAX_PACKET = 8192

PORT = int(os.environ.get("RELAY_PORT", "22600"))
MAX_ROOMS = int(os.environ.get("RELAY_MAX_ROOMS", "100"))
RATE = int(os.environ.get("RELAY_RATE_KBPS", "2048")) * 1024
ROOM_RE = re.compile(r"^[a-z0-9_-]{1,15}$")


def log(msg):
    print(time.strftime("%Y-%m-%d %H:%M:%S ") + msg, flush=True)


class Room:
    def __init__(self, name, host):
        self.name = name
        self.host = host
        self.info = b""
        self.peers = {}          # peer id -> address
        self.started = False
        self.race_players = 0


rooms = {}                   # name -> Room
who = {}                     # address -> (room name, peer id; 0 = host)
links = {}                   # lobby room name -> {member id: address}
link_who = {}                # address -> (lobby room name, member id)
seen = {}                    # address -> last packet time
bucket = {}                  # address -> [tokens, last refill]


def allowed(addr, size, now):
    b = bucket.get(addr)
    if b is None:
        b = bucket[addr] = [RATE, now]
    b[0] = min(RATE, b[0] + (now - b[1]) * RATE)
    b[1] = now
    if b[0] < size:
        return False
    b[0] -= size
    return True


def room_name(raw):
    name = raw.split(b"\0", 1)[0].decode("ascii", "replace").lower()
    return name if ROOM_RE.match(name) else None


def error(sock, addr, code, text):
    sock.sendto(MAGIC + bytes([ERROR, code]) + text.encode()[:40], addr)


def status(sock, room):
    mask = 0
    for p in room.peers:
        mask |= 1 << p
    sock.sendto(MAGIC + bytes([STATUS, int(room.started), len(room.peers)]) + mask.to_bytes(2, "little"), room.host)


def welcome(sock, room, peer):
    sock.sendto(MAGIC + bytes([WELCOME, peer, int(room.started), room.race_players]) + room.info, room.peers[peer])


def close_room(sock, room, why):
    for p, a in room.peers.items():
        error(sock, a, E_HOST_LEFT, "host left")
        who.pop(a, None)
    who.pop(room.host, None)
    rooms.pop(room.name, None)
    log("room %s closed (%s)" % (room.name, why))


def drop_peer(sock, room, peer, why):
    a = room.peers.pop(peer, None)
    if a is not None:
        who.pop(a, None)
        log("room %s: player %d left (%s), %d joined" % (room.name, peer + 1, why, len(room.peers)))
        status(sock, room)


def link_status(sock, name):
    members = links.get(name, {})
    mask = 0
    for m in members:
        mask |= 1 << m
    for m, a in members.items():
        sock.sendto(MAGIC + bytes([LINK_STATUS, m]) + mask.to_bytes(2, "little"), a)


def link_leave(sock, addr, why):
    w = link_who.pop(addr, None)
    if not w:
        return
    members = links.get(w[0], {})
    members.pop(w[1], None)
    log("lobby %s: cabinet %d left (%s), %d in" % (w[0], w[1] + 1, why, len(members)))
    if members:
        link_status(sock, w[0])
    else:
        links.pop(w[0], None)
        log("lobby %s closed" % w[0])


def handle_link(sock, kind, data, addr, now):
    if kind == LINK_HELLO:
        if len(data) < JOIN_HELLO_LEN:
            return
        if data[5] != VERSION:
            return error(sock, addr, E_VERSION, "version")
        name = room_name(data[6:6 + ROOM_LEN])
        if not name:
            return
        w = link_who.get(addr)
        if w and w[0] != name:
            link_leave(sock, addr, "changed room")
            w = None
        seen[addr] = now
        if w:                                           # keep-alive: answer this member only
            mask = 0
            for m in links[name]:
                mask |= 1 << m
            return sock.sendto(MAGIC + bytes([LINK_STATUS, w[1]]) + mask.to_bytes(2, "little"), addr)
        members = links.get(name)
        if members is None:
            if len(links) + len(rooms) >= MAX_ROOMS:
                return error(sock, addr, E_SERVER_FULL, "server full")
            members = links[name] = {}
            log("lobby %s opened" % name)
        free = [m for m in range(LINK_MEMBERS) if m not in members]
        if not free:
            return error(sock, addr, E_FULL, "full")
        members[free[0]] = addr
        link_who[addr] = (name, free[0])
        log("lobby %s: cabinet %d in, %d in" % (name, free[0] + 1, len(members)))
        return link_status(sock, name)
    w = link_who.get(addr)
    if not w:
        return
    members = links.get(w[0], {})
    seen[addr] = now
    if kind == LINK_BCAST:
        out = MAGIC + bytes([LINK_BCAST, w[1]]) + data[5:]
        for m, a in members.items():
            if m != w[1]:
                sock.sendto(out, a)
    elif kind == LINK_DATA and len(data) >= 7:
        a = members.get(data[5])
        if a:
            sock.sendto(MAGIC + bytes([LINK_DATA, w[1], data[6]]) + data[7:], a)
    elif kind == BYE:
        link_leave(sock, addr, "finished")


def handle(sock, data, addr, now):
    if len(data) < 5 or data[:4] != MAGIC:
        return
    kind = data[4]
    if kind in (LINK_HELLO, LINK_BCAST, LINK_DATA) or (kind == BYE and addr in link_who):
        return handle_link(sock, kind, data, addr, now)
    if kind == DATA:
        w = who.get(addr)
        if not w or w[0] not in rooms:
            return
        room = rooms[w[0]]
        seen[addr] = now
        if w[1] == 0:                                   # host -> one player
            if len(data) < 6:
                return
            a = room.peers.get(data[5])
            if a:
                sock.sendto(MAGIC + bytes([DATA]) + data[6:], a)
        else:                                           # player -> host
            sock.sendto(MAGIC + bytes([DATA, w[1]]) + data[5:], room.host)
        return

    if kind == HOST_HELLO:
        if len(data) < 6 + ROOM_LEN + INFO_LEN:
            return
        if data[5] != VERSION:
            return error(sock, addr, E_VERSION, "version")
        name = room_name(data[6:6 + ROOM_LEN])
        if not name:
            return
        room = rooms.get(name)
        if room and room.host != addr:
            if now - seen.get(room.host, 0) < IDLE_SECONDS:
                return error(sock, addr, E_ROOM_TAKEN, "room taken")
            close_room(sock, room, "host replaced")
            room = None
        if room is None:
            if len(rooms) >= MAX_ROOMS:
                return error(sock, addr, E_SERVER_FULL, "server full")
            old = who.get(addr)                         # same PC hosting a new room
            if old and old[0] in rooms:
                if old[1] == 0:
                    close_room(sock, rooms[old[0]], "host moved")
                else:
                    drop_peer(sock, rooms[old[0]], old[1], "now hosting")
            room = rooms[name] = Room(name, addr)
            who[addr] = (name, 0)
            log("room %s opened" % name)
        seen[addr] = now
        if not room.started:
            room.info = data[6 + ROOM_LEN:6 + ROOM_LEN + INFO_LEN]
        return status(sock, room)

    if kind == JOIN_HELLO:
        if len(data) < JOIN_HELLO_LEN:
            return
        if data[5] != VERSION:
            return error(sock, addr, E_VERSION, "version")
        name = room_name(data[6:6 + ROOM_LEN])
        room = rooms.get(name) if name else None
        if room is None:
            return error(sock, addr, E_NO_ROOM, "no room")
        w = who.get(addr)
        if w and w[0] == name and w[1] > 0:
            seen[addr] = now
            return welcome(sock, room, w[1])
        if room.started:
            return error(sock, addr, E_STARTED, "started")
        free = [p for p in range(1, MAX_PEERS + 1) if p not in room.peers]
        if not free or addr == room.host:
            return error(sock, addr, E_FULL, "full")
        if w and w[0] in rooms:                         # was in another room
            other = rooms[w[0]]
            if w[1] == 0:
                close_room(sock, other, "host joined another room")
            else:
                drop_peer(sock, other, w[1], "joined another room")
        peer = free[0]
        room.peers[peer] = addr
        who[addr] = (name, peer)
        seen[addr] = now
        log("room %s: player %d joined, %d joined" % (name, peer + 1, len(room.peers)))
        welcome(sock, room, peer)
        return status(sock, room)

    w = who.get(addr)
    if not w or w[0] not in rooms:
        return
    room = rooms[w[0]]
    seen[addr] = now
    if kind == START and w[1] == 0:
        if not room.started:
            room.started = True
            room.race_players = len(room.peers) + 1
            log("room %s: race started with %d players" % (room.name, room.race_players))
            for p in room.peers:
                welcome(sock, room, p)
        status(sock, room)
    elif kind == BYE:
        if w[1] == 0:
            close_room(sock, room, "host finished")
        else:
            drop_peer(sock, room, w[1], "finished")


def cleanup(sock, now):
    for room in list(rooms.values()):
        if now - seen.get(room.host, 0) > IDLE_SECONDS:
            close_room(sock, room, "host silent")
            continue
        for p, a in list(room.peers.items()):
            if now - seen.get(a, 0) > IDLE_SECONDS:
                drop_peer(sock, room, p, "silent")
    for a in [a for a in link_who if now - seen.get(a, 0) > LINK_IDLE]:
        link_leave(sock, a, "silent")
    for a in [a for a, t in seen.items() if now - t > IDLE_SECONDS * 3 and a not in who and a not in link_who]:
        seen.pop(a, None)
        bucket.pop(a, None)


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", PORT))
    sock.settimeout(1.0)
    log("NASCAR relay listening on UDP %d (max %d rooms, %d KB/s per player)" % (PORT, MAX_ROOMS, RATE // 1024))
    last_clean = time.monotonic()
    while True:
        try:
            data, addr = sock.recvfrom(MAX_PACKET)
        except socket.timeout:
            data = None
        except OSError:                                 # e.g. ICMP "port unreachable" from a player who left
            continue
        now = time.monotonic()
        if data and allowed(addr, len(data), now):
            try:
                handle(sock, data, addr, now)
            except OSError as e:
                log("send failed: %s" % e)
        if now - last_clean >= 1.0:
            cleanup(sock, now)
            last_clean = now


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
