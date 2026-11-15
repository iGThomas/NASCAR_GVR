// NascarNet - linked races through a relay server or on the same network. See NascarNet.h.
//
// Relay protocol (UDP, every message starts with "NGR1" + a type byte; the server is
// relay/nascar_relay.py in the repo, which documents the same layout):
//   HOST_HELLO  host -> relay   ver, room[16], info[96]            every 2 s (keeps the room open)
//   STATUS      relay -> host   started, players in room, peer mask
//   JOIN_HELLO  join -> relay   ver, room[16], padded to 128 bytes every 2 s
//   WELCOME     relay -> join   peer id, started, race player count, info[96]
//   START       host -> relay   room closes to new players; relay sends WELCOME(started) to all
//   DATA        join -> relay   payload                      relay -> host  peer, payload
//               host -> relay   peer, payload                relay -> join  payload
//   ERROR       relay -> any    code, text
//   BYE         any -> relay    leaving
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include "NascarNet.h"

#pragma comment(lib, "ws2_32.lib")
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

enum { T_HOST_HELLO = 1, T_JOIN_HELLO = 2, T_START = 3, T_DATA = 4, T_ERROR = 5, T_BYE = 6,
       T_STATUS = 7, T_WELCOME = 8 };
enum { E_ROOM_TAKEN = 1, E_NO_ROOM = 2, E_STARTED = 3, E_FULL = 4, E_VERSION = 5, E_SERVER_FULL = 6,
       E_HOST_LEFT = 7 };
static const unsigned char kMagic[4] = { 'N', 'G', 'R', '1' };
static const int kVersion = 1, kRoomLen = 16, kInfoLen = 96, kJoinHelloLen = 128, kMaxPeers = 9;   // 10 players, as the arcade link
static const unsigned short kGamePort = 22544, kDefaultRelayPort = 22600;

static NetLogFn g_log;

// ---------------------------------------------------------------- agent state

struct Agent {
    CRITICAL_SECTION cs;
    bool host;
    sockaddr_in relay;
    char room[kRoomLen + 1];
    char info[kInfoLen + 1];         // host: what it sends; join: what the host sent
    // shared with the lobby window / launcher (under cs)
    int  phase;                      // 0 connecting, 1 in room, 2 started, 3 failed
    int  waitCode;                   // join: last "not yet" error (room not open yet)
    std::string error;
    int  peers;                      // host: players joined besides the host
    int  myPeer;                     // join: this player's number from the relay (1..9)
    int  raceCount;                  // players in the race (fixed at start)
    bool raceRunning;                // host: its race has been started
    DWORD raceStart;
    volatile bool stop;
    // owned by the agent thread
    SOCKET relaySock;
    SOCKET local[kMaxPeers + 1];     // host: one per peer; join: local[0] = 127.0.0.1:22544
    sockaddr_in raceAddr;            // join: where our race sends from
    bool haveRace;
    DWORD lastRelay, lastHello, lastStart;
    unsigned long toRelay, fromRelay;
    unsigned long long bytesTo, bytesFrom;
    DWORD firstData;                 // first race packet (for the average rate in the log)
};

static void set_failed(Agent* a, const char* why)
{
    EnterCriticalSection(&a->cs);
    if (a->phase != 3) { a->phase = 3; a->error = why; }
    LeaveCriticalSection(&a->cs);
    g_log("multiplayer: %s", why);
}

static void no_connreset(SOCKET s)
{
    // a UDP send to a port nobody listens on yet (the race still loading) must not make the
    // next recvfrom fail with WSAECONNRESET
    BOOL off = FALSE; DWORD n = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), NULL, 0, &n, NULL, NULL);
}

static SOCKET udp_bound(unsigned long ip, unsigned short port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return s;
    sockaddr_in a = { 0 };
    a.sin_family = AF_INET; a.sin_addr.s_addr = ip; a.sin_port = htons(port);
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); return INVALID_SOCKET; }
    no_connreset(s);
    return s;
}

static void relay_send(Agent* a, const unsigned char* p, int n)
{
    sendto(a->relaySock, (const char*)p, n, 0, (sockaddr*)&a->relay, sizeof(a->relay));
}

static void send_simple(Agent* a, int type)
{
    unsigned char b[5]; memcpy(b, kMagic, 4); b[4] = (unsigned char)type; relay_send(a, b, 5);
}

static void send_hello(Agent* a)
{
    unsigned char b[kJoinHelloLen] = { 0 };
    memcpy(b, kMagic, 4);
    b[4] = (unsigned char)(a->host ? T_HOST_HELLO : T_JOIN_HELLO);
    b[5] = (unsigned char)kVersion;
    memcpy(b + 6, a->room, strlen(a->room));
    int n = kJoinHelloLen;
    if (a->host) { memcpy(b + 6 + kRoomLen, a->info, strlen(a->info)); n = 6 + kRoomLen + kInfoLen; }
    relay_send(a, b, n);
}

static const char* error_text(int code)
{
    switch (code) {
    case E_ROOM_TAKEN:  return "This room name is already used by another host. Pick another Room in nascar_settings.ini.";
    case E_NO_ROOM:     return "Room not open yet - waiting for the host.";
    case E_STARTED:     return "This room's race has already started.";
    case E_FULL:        return "This room is full (10 players).";
    case E_VERSION:     return "The relay server runs a different version. Update the game or the relay.";
    case E_SERVER_FULL: return "The relay server is full right now. Try again later.";
    case E_HOST_LEFT:   return "The host left the room.";
    }
    return "The relay server refused the connection.";
}

static void on_relay_packet(Agent* a, const unsigned char* p, int n)
{
    if (n < 5 || memcmp(p, kMagic, 4) != 0) return;
    a->lastRelay = GetTickCount();
    a->fromRelay++; a->bytesFrom += n;
    int type = p[4];
    if (type == T_DATA) {
        if (a->host) {
            if (n < 6) return;
            int peer = p[5];
            if (peer < 1 || peer > kMaxPeers) return;
            if (a->local[peer] == INVALID_SOCKET) {
                // each player gets its own loopback address, so the race tells them apart even if
                // it keys clients by IP alone
                a->local[peer] = udp_bound(htonl(0x7F000001 + peer), 0);
                if (a->local[peer] == INVALID_SOCKET) { g_log("multiplayer: no local socket for player %d", peer + 1); return; }
                g_log("multiplayer: player %d connected through the relay", peer + 1);
            }
            // the race listens on every address; 127.0.0.254 reaches only it, even when a joining
            // agent on the same PC (tests) holds 127.0.0.1:22544
            sockaddr_in race = { 0 };
            race.sin_family = AF_INET; race.sin_addr.s_addr = htonl(0x7F0000FE); race.sin_port = htons(kGamePort);
            sendto(a->local[peer], (const char*)p + 6, n - 6, 0, (sockaddr*)&race, sizeof(race));
        } else if (a->haveRace) {
            sendto(a->local[0], (const char*)p + 5, n - 5, 0, (sockaddr*)&a->raceAddr, sizeof(a->raceAddr));
        }
        return;
    }
    EnterCriticalSection(&a->cs);
    if (type == T_STATUS && a->host && n >= 9) {
        if (a->phase == 0) { a->phase = 1; g_log("multiplayer: room '%s' open on the relay", a->room); }
        if (a->peers != p[6]) g_log("multiplayer: %d player(s) joined", p[6]);
        a->peers = p[6];
        if (p[5] && a->phase < 2) { a->phase = 2; g_log("multiplayer: relay confirmed the start"); }
    } else if (type == T_WELCOME && !a->host && n >= 8) {
        if (a->phase == 0) g_log("multiplayer: joined room '%s' as player %d", a->room, p[5] + 1);
        a->waitCode = 0;
        a->myPeer = p[5];
        if (a->phase < 1) a->phase = 1;
        memset(a->info, 0, sizeof(a->info));
        memcpy(a->info, p + 8, (n - 8) < kInfoLen ? (n - 8) : kInfoLen);
        if (p[6] && a->phase < 2) {
            a->raceCount = p[7];
            a->phase = 2;
            g_log("multiplayer: host started the race (%d players): %s", a->raceCount, a->info);
        }
    } else if (type == T_ERROR && n >= 6) {
        int code = p[5];
        if (!a->host && code == E_NO_ROOM && a->phase < 2) {
            if (a->waitCode != code) g_log("multiplayer: room '%s' not open yet, waiting for the host", a->room);
            a->waitCode = code;
        } else if (a->phase == 2 && code == E_HOST_LEFT) {
            g_log("multiplayer: host left the room");
        } else if (a->phase < 2) {
            LeaveCriticalSection(&a->cs);
            set_failed(a, error_text(code));
            return;
        }
    }
    LeaveCriticalSection(&a->cs);
}

static DWORD WINAPI agent_thread(LPVOID pv)
{
    Agent* a = (Agent*)pv;
    unsigned char buf[8192];
    a->lastRelay = GetTickCount();
    DWORD joinedAt = 0;
    while (!a->stop) {
        DWORD now = GetTickCount();
        if (now - a->lastHello >= 2000) { send_hello(a); a->lastHello = now; }

        EnterCriticalSection(&a->cs);
        int phase = a->phase; bool raceRunning = a->raceRunning; DWORD raceStart = a->raceStart;
        LeaveCriticalSection(&a->cs);
        if (phase == 3) break;
        if (phase >= 1 && !joinedAt) joinedAt = now;
        // the host's race needs a moment to open port 22544 before the others' races knock
        if (a->host && raceRunning && phase < 2 && now - raceStart >= 3000 && now - a->lastStart >= 500) {
            send_simple(a, T_START); a->lastStart = now;
        }
        if (phase < 2 && now - a->lastRelay > 10000)
            set_failed(a, phase == 0 ? "No answer from the relay server. Check Relay in nascar_settings.ini and your internet connection."
                                     : "Lost the connection to the relay server.");

        fd_set rd; FD_ZERO(&rd);
        FD_SET(a->relaySock, &rd);
        for (int i = 0; i <= kMaxPeers; i++) if (a->local[i] != INVALID_SOCKET) FD_SET(a->local[i], &rd);
        timeval tv = { 0, 100 * 1000 };
        if (select(0, &rd, NULL, NULL, &tv) <= 0) continue;

        if (FD_ISSET(a->relaySock, &rd)) {
            int n = recvfrom(a->relaySock, (char*)buf, sizeof(buf), 0, NULL, NULL);
            if (n > 0) on_relay_packet(a, buf, n);
        }
        for (int i = 0; i <= kMaxPeers; i++) {
            if (a->local[i] == INVALID_SOCKET || !FD_ISSET(a->local[i], &rd)) continue;
            sockaddr_in from; int fl = sizeof(from);
            int off = a->host ? 6 : 5;               // host adds the player's number after the type
            int n = recvfrom(a->local[i], (char*)buf + off, sizeof(buf) - off, 0, (sockaddr*)&from, &fl);
            if (n <= 0) continue;
            memcpy(buf, kMagic, 4); buf[4] = T_DATA;
            if (a->host) buf[5] = (unsigned char)i;
            else {
                if (!a->haveRace) g_log("multiplayer: race connected to the agent");
                a->raceAddr = from; a->haveRace = true;
            }
            relay_send(a, buf, n + off);
            a->toRelay++; a->bytesTo += n + off;
            if (!a->firstData) a->firstData = GetTickCount();
        }
    }
    send_simple(a, T_BYE);
    return 0;
}

// ---------------------------------------------------------------- lobby window

struct Lobby { Agent* a; const NetSettings* ns; int result; HWND text, startBtn; std::string last; };
static Lobby* g_lobby;
enum { ID_START = 101, ID_CANCEL = 102, ID_TIMER = 1 };

static std::string info_value(const char* info, const char* key)
{
    std::string s(info), k = std::string(key) + "=";
    size_t i = s.find(k);
    if (i == std::string::npos) return "";
    i += k.size();
    size_t e = s.find(';', i);
    return s.substr(i, e == std::string::npos ? std::string::npos : e - i);
}

static void lobby_refresh(HWND w)
{
    Lobby* L = g_lobby; Agent* a = L->a;
    EnterCriticalSection(&a->cs);
    int phase = a->phase, peers = a->peers, waitCode = a->waitCode;
    std::string err = a->error, info = a->info;
    LeaveCriticalSection(&a->cs);
    if (phase == 3) { L->result = 3; DestroyWindow(w); return; }
    if (!a->host && phase == 2) { L->result = 1; DestroyWindow(w); return; }

    char t[600];
    std::string track = info_value(info.c_str(), "track"), laps = info_value(info.c_str(), "laps");
    if (phase == 0 && waitCode == 0)
        snprintf(t, sizeof(t), "Room: %s\r\n\r\nConnecting to the relay server...", a->room);
    else if (a->host)
        snprintf(t, sizeof(t), "Room: %s\r\nPlayers in: %d of %d\r\nRace: %s, %s laps\r\n\r\n"
                 "The race starts when everyone is in. \"Start now\" races with the players already here.",
                 a->room, peers + 1, L->ns->players, track.c_str(), laps.c_str());
    else if (waitCode)
        snprintf(t, sizeof(t), "Room: %s\r\n\r\nThe room is not open yet - waiting for the host...", a->room);
    else
        snprintf(t, sizeof(t), "Room: %s\r\nRace: %s, %s laps\r\n\r\nJoined. Waiting for the host to start the race...",
                 a->room, track.c_str(), laps.c_str());
    if (L->last != t) { SetWindowTextA(L->text, t); L->last = t; }
    if (L->startBtn) EnableWindow(L->startBtn, phase >= 1 && peers >= 1);
    if (a->host && phase >= 1 && peers >= L->ns->players - 1) { L->result = 1; DestroyWindow(w); }
}

static LRESULT CALLBACK lobby_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_TIMER: lobby_refresh(w); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_START) { g_lobby->result = 1; DestroyWindow(w); }
        if (LOWORD(wp) == ID_CANCEL) { g_lobby->result = 2; DestroyWindow(w); }
        return 0;
    case WM_CLOSE: g_lobby->result = 2; DestroyWindow(w); return 0;
    case WM_DESTROY: KillTimer(w, ID_TIMER); PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}

// 1 = race now, 2 = cancelled, 3 = failed
static int lobby_run(Agent* a, const NetSettings& ns)
{
    Lobby L = { a, &ns, 2, NULL, NULL, "" };
    g_lobby = &L;
    HINSTANCE hi = GetModuleHandleA(NULL);
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = lobby_proc; wc.hInstance = hi; wc.lpszClassName = "NascarLobby";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconA(hi, MAKEINTRESOURCEA(1));
    RegisterClassA(&wc);
    UINT dpi = 96;
    HMODULE u = GetModuleHandleA("user32.dll");
    typedef UINT (WINAPI *GetDpiForSystemFn)(void);
    GetDpiForSystemFn gd = u ? (GetDpiForSystemFn)GetProcAddress(u, "GetDpiForSystem") : NULL;
    if (gd) dpi = gd();
#define S(v) MulDiv((v), (int)dpi, 96)
    RECT r = { 0, 0, S(440), S(190) };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    HWND w = CreateWindowExA(0, "NascarLobby", a->host ? "NASCAR - hosting a race" : "NASCAR - joining a race", style,
                             (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2,
                             ww, wh, NULL, NULL, hi, NULL);
    if (!w) return 3;
    NONCLIENTMETRICSA ncm = { sizeof(ncm) };
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfHeight = -S(15);
    HFONT f = CreateFontIndirectA(&ncm.lfMessageFont);
    L.text = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, S(16), S(14), S(408), S(120), w, NULL, hi, NULL);
    if (a->host)
        L.startBtn = CreateWindowExA(0, "BUTTON", "Start now", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
                                     S(224), S(146), S(96), S(30), w, (HMENU)ID_START, hi, NULL);
    HWND cancel = CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  S(328), S(146), S(96), S(30), w, (HMENU)ID_CANCEL, hi, NULL);
#undef S
    SendMessageA(L.text, WM_SETFONT, (WPARAM)f, TRUE);
    if (L.startBtn) SendMessageA(L.startBtn, WM_SETFONT, (WPARAM)f, TRUE);
    SendMessageA(cancel, WM_SETFONT, (WPARAM)f, TRUE);
    lobby_refresh(w);
    if (!IsWindow(w)) { DeleteObject(f); return L.result; }   // already decided (e.g. failed at once)
    ShowWindow(w, SW_SHOW);
    SetForegroundWindow(w);
    SetTimer(w, ID_TIMER, 200, NULL);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    DeleteObject(f);
    g_lobby = NULL;
    return L.result;
}

// ---------------------------------------------------------------- setup

static bool resolve_relay(const std::string& relay, sockaddr_in& out, std::string& err)
{
    std::string host = relay, port;
    size_t c = relay.rfind(':');
    if (c != std::string::npos) { host = relay.substr(0, c); port = relay.substr(c + 1); }
    if (port.empty()) { char b[8]; snprintf(b, sizeof(b), "%u", kDefaultRelayPort); port = b; }
    addrinfo hints = { 0 }, *res = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        err = "Cannot find the relay server \"" + relay + "\". Check Relay in nascar_settings.ini and your internet connection.";
        return false;
    }
    out = *(sockaddr_in*)res->ai_addr;
    freeaddrinfo(res);
    return true;
}

static bool clean_room(const std::string& in, char* out)
{
    int n = 0;
    for (size_t i = 0; i < in.size() && n < kRoomLen - 1; i++) {
        char ch = (char)tolower((unsigned char)in[i]);
        if (isalnum((unsigned char)ch) || ch == '-' || ch == '_') out[n++] = ch;
    }
    out[n] = 0;
    return n > 0;
}

static int fail_box(const std::string& why)
{
    g_log("multiplayer: %s", why.c_str());
    MessageBoxA(NULL, why.c_str(), "NASCAR - multiplayer", MB_OK | MB_ICONWARNING);
    return 1;
}

int net_run(const NetSettings& ns, NetRaceArgsFn raceArgs, NetLaunchFn launch, NetLogFn log)
{
    g_log = log;
    bool host = ns.mode == "host";
    int players = ns.players < 2 ? 2 : (ns.players > kMaxPeers + 1 ? kMaxPeers + 1 : ns.players);
    char args[1200];

    if (ns.relay.empty()) {
        // same network: the race's own client/server, nothing in between
        if (host) {
            snprintf(args, sizeof(args), "-mode multi -server -playernumber 1 -playercount %d %s",
                     players, raceArgs(ns.track, ns.series, ns.laps).c_str());
        } else {
            unsigned b[4]; char extra;
            if (sscanf(ns.hostAddress.c_str(), "%u.%u.%u.%u%c", &b[0], &b[1], &b[2], &b[3], &extra) != 4 ||
                b[0] > 255 || b[1] > 255 || b[2] > 255 || b[3] > 255)
                return fail_box("To join without a relay, set HostAddress in nascar_settings.ini to the host's IP address "
                                "(numbers like 192.168.1.20), or set Relay and Room.");
            // the race wants a player number; it only names the car ("Player N")
            snprintf(args, sizeof(args), "-mode multi -client %s -playernumber 2 -playercount %d %s +allowmultiple",
                     ns.hostAddress.c_str(), players, raceArgs(ns.track, ns.series, ns.laps).c_str());
        }
        log("multiplayer: %s on the local network", host ? "hosting" : "joining");
        unsigned long code = launch(args);
        return (int)code;
    }

    char room[kRoomLen + 1];
    if (!clean_room(ns.room, room)) strcpy(room, "public");   // no Room = the shared public room
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return fail_box("Windows networking could not start.");

    Agent* a = new Agent();
    InitializeCriticalSection(&a->cs);
    a->host = host;
    strcpy(a->room, room);
    for (int i = 0; i <= kMaxPeers; i++) a->local[i] = INVALID_SOCKET;
    std::string err;
    if (!resolve_relay(ns.relay, a->relay, err)) { WSACleanup(); return fail_box(err); }
    a->relaySock = udp_bound(INADDR_ANY, 0);
    if (a->relaySock == INVALID_SOCKET) { WSACleanup(); return fail_box("Could not open a network connection."); }
    if (host) {
        snprintf(a->info, sizeof(a->info), "track=%s;series=%s;laps=%d", ns.track.c_str(), ns.series.c_str(), ns.laps);
    } else {
        a->local[0] = udp_bound(htonl(0x7F000001), kGamePort);
        if (a->local[0] == INVALID_SOCKET) {
            closesocket(a->relaySock); WSACleanup();
            return fail_box("Network port 22544 is busy on this PC - is another copy of the game running? Close it and try again.");
        }
    }
    char ip[32]; inet_ntop(AF_INET, &a->relay.sin_addr, ip, sizeof(ip));
    log("multiplayer: %s room '%s' through relay %s (%s:%u)", host ? "hosting" : "joining", room, ns.relay.c_str(),
        ip, ntohs(a->relay.sin_port));

    HANDLE th = CreateThread(NULL, 0, agent_thread, a, 0, NULL);
    int r = lobby_run(a, ns);
    int code = 0;
    if (r == 1) {
        if (host) {
            EnterCriticalSection(&a->cs);
            a->raceCount = a->peers + 1; a->raceRunning = true; a->raceStart = GetTickCount();
            int count = a->raceCount;
            LeaveCriticalSection(&a->cs);
            log("multiplayer: starting the race with %d players", count);
            snprintf(args, sizeof(args), "-mode multi -server -playernumber 1 -playercount %d %s",
                     count, raceArgs(ns.track, ns.series, ns.laps).c_str());
        } else {
            EnterCriticalSection(&a->cs);
            std::string info = a->info; int peer = a->myPeer, count = a->raceCount;
            LeaveCriticalSection(&a->cs);
            std::string track = info_value(info.c_str(), "track"), series = info_value(info.c_str(), "series");
            int laps = atoi(info_value(info.c_str(), "laps").c_str());
            if (track.empty()) track = ns.track;
            if (laps <= 0) laps = ns.laps;
            snprintf(args, sizeof(args), "-mode multi -client 127.0.0.1 -playernumber %d -playercount %d %s +allowmultiple",
                     peer + 1, count < 2 ? 2 : count, raceArgs(track, series, laps).c_str());
        }
        code = (int)launch(args);
    } else if (r == 3) {
        EnterCriticalSection(&a->cs); err = a->error; LeaveCriticalSection(&a->cs);
        fail_box(err);
        code = 1;
    } else {
        log("multiplayer: cancelled");
    }
    a->stop = true;
    WaitForSingleObject(th, 3000);
    CloseHandle(th);
    double secs = a->firstData ? (GetTickCount() - a->firstData) / 1000.0 : 0;
    if (secs < 1) secs = 1;
    log("multiplayer: done - to the relay %lu packets / %llu KB, from it %lu / %llu KB; average over %.0f s: "
        "up %.0f KB/s %.0f packets/s, down %.0f KB/s", a->toRelay, a->bytesTo / 1024, a->fromRelay,
        a->bytesFrom / 1024, secs, a->bytesTo / 1024.0 / secs, a->toRelay / secs, a->bytesFrom / 1024.0 / secs);
    closesocket(a->relaySock);
    for (int i = 0; i <= kMaxPeers; i++) if (a->local[i] != INVALID_SOCKET) closesocket(a->local[i]);
    WSACleanup();
    return code;
}
