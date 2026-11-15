// GvrLinking.dll - the menus' cabinet link, rebuilt over the internet relay.
//
// The original (GlobalVR, 2007) linked cabinets with IPX broadcasts, which Windows no longer has, and
// its SuspendPackets / SetPacketDataBlock wait on a worker that never starts without IPX - the shell
// hung with the link plug-in enabled. This replacement keeps the same 26 exports (the plug-in
// GvrLinkingPlugIn.dll imports 13 of them by name) and links the cabinets through the relay room
// set in nascar_settings.ini ([Multiplayer] Mode=lobby, Relay, Room).
//
// What the menus see (NASCAR_Selection.am, behaviour CabinetComm):
//   * a table of up to 10 cabinets, OWN cabinet included; entry n = relay member n, online while
//     the relay lists it
//   * fake addresses: MAC "0200000000<nn>" (lowest member = lowest MAC = race server) and IP
//     10.127.0.<n+1> (the script ignores 127.x addresses). The race gets "-client 10.127.0.<s+1>";
//     the GvrIO shim in the race turns that into 127.10.0.<s+1> before the race reads it.
//   * every text block the script sends goes to the others and comes back to us, as IPX did
// Race traffic: on a joining PC the race sends to 127.10.0.<s+1>:22544, where this DLL listens and
// passes it to member s through the relay; on the server PC each remote player gets a local socket
// 127.0.0.<m+1> that talks to the race on 127.0.0.254:22544 (the race listens on every address).
//
// Rules kept from the original's pitfalls: no call ever waits, nothing starts in the constructor
// (it runs under the loader lock), the network thread starts from SetPacketReceiveCallback.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "ws2_32.lib")
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

struct sockaddr_ipx;

namespace Gvr { namespace Zeus { namespace GvrLinking {

// The original's 104-byte packet header; the plug-in reads +92 (data length) and +100 (data).
struct sGvrPacket {
    unsigned long drop;          // +0
    unsigned long tick;          // +4
    unsigned long game[11];      // +8
    char          mac[13];       // +52
    char          ip[16];        // +65
    char          pad[3];        // +81
    unsigned long session;       // +84
    unsigned long cabinet;       // +88
    unsigned long length;        // +92
    unsigned long checksum;      // +96
    void*         data;          // +100
};

class __declspec(dllexport) GvrLinking {
public:
    GvrLinking();
    ~GvrLinking();
    GvrLinking& operator=(const GvrLinking& o);
    bool GetAdapterDHCPIP(char* out, int n);
    bool GetAdapterName(char* out, int n);
    int  GetCabIndex();
    int  GetCabinetNumber();
    int  GetCabsAvailable();
    bool GetIPAddress(char* out, int n, int cab);
    bool GetInvalidIP(char* ip);
    bool GetLocalIP(char* out, int n);
    bool GetMacAddress(char* out, int n);
    bool GetMacAddress(char* out, int n, int cab);
    unsigned long GetNextSession();
    int  GetNumCabinetsOnline();
    sGvrPacket* GetPacket(int cab);
    unsigned long GetPacketsReceived();
    bool IsOnline(int cab);
    bool IsOnline(char* status, int n);
    bool SetCabinetNumber(int number);
    void SetPacketDataBlock(unsigned long size, void* data);
    bool SetPacketReceiveCallback(void* (__cdecl* cb)(int, sGvrPacket*));
    void SetSession(unsigned long session);
    void SuspendPackets(bool suspend);
private:
    int  NetIPXSocket(unsigned short port);
    void NetadrToSockadr(unsigned short port, sockaddr_ipx* a, unsigned int node);
    unsigned char m_opaque[0x434];   // the plug-in reserves exactly this much for the object
};

} } }

using namespace Gvr::Zeus::GvrLinking;

// ---------------------------------------------------------------- state

enum { T_BYE = 6, T_ERROR = 5, T_LINK_HELLO = 9, T_LINK_STATUS = 10, T_LINK_BCAST = 11, T_LINK_DATA = 12 };
enum { DIR_TO_SERVER = 1, DIR_TO_CLIENT = 2 };
static const unsigned char kMagic[4] = { 'N', 'G', 'R', '1' };
static const int kCabs = 10, kRoomLen = 16, kHelloLen = 128, kVersion = 1;
static const unsigned short kGamePort = 22544, kDefaultRelayPort = 22600;

struct Cab {
    bool       online;
    sGvrPacket pkt;
};

static CRITICAL_SECTION g_cs;
static Cab   g_cab[kCabs];
static int   g_self = -1;                         // our member id, -1 until the relay answers
static int   g_cabNumber = -1;                    // what the menus set (used while offline)
static void* (__cdecl* g_cb)(int, sGvrPacket*);
static volatile bool g_suspended, g_stop, g_started;
static unsigned long g_received;
static char  g_logPath[MAX_PATH];
static char  g_root[MAX_PATH];                     // install root (Shell\bin\.. \..)

// own text blocks waiting to be handed back to the menus (the original got them back from the wire)
struct Block { int len; char* data; };
static Block g_own[64];
static int   g_ownHead, g_ownTail;

static void logf(const char* fmt, ...)
{
    if (!g_logPath[0]) return;
    FILE* f = fopen(g_logPath, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void fake_mac(int cab, char* out, int n) { _snprintf(out, n, "0200000000%02x", cab); out[n - 1] = 0; }
static void fake_ip(int cab, char* out, int n)  { _snprintf(out, n, "10.127.0.%d", cab + 1); out[n - 1] = 0; }

// ---------------------------------------------------------------- exports

GvrLinking::GvrLinking() { memset(m_opaque, 0, sizeof(m_opaque)); }
GvrLinking::~GvrLinking() { g_stop = true; }      // process exit: never wait under the loader lock
GvrLinking& GvrLinking::operator=(const GvrLinking& o) { memcpy(m_opaque, o.m_opaque, sizeof(m_opaque)); return *this; }

bool GvrLinking::GetAdapterDHCPIP(char* out, int n) { if (out && n > 0) out[0] = 0; return false; }
bool GvrLinking::GetAdapterName(char* out, int n)   { if (out && n > 0) out[0] = 0; return false; }

int GvrLinking::GetCabIndex()
{
    EnterCriticalSection(&g_cs); int s = g_self; LeaveCriticalSection(&g_cs);
    return s < 0 ? 0 : s;
}

// the cabinet number the menus show and give the race: member + 1 while linked (unique in the room)
int GvrLinking::GetCabinetNumber()
{
    EnterCriticalSection(&g_cs);
    int r = (g_self >= 0 && g_cab[g_self].online) ? g_self + 1 : g_cabNumber;
    LeaveCriticalSection(&g_cs);
    return r;
}

int GvrLinking::GetCabsAvailable() { return GetNumCabinetsOnline(); }

bool GvrLinking::GetIPAddress(char* out, int n, int cab)
{
    if (!out || n < 16 || cab < 0 || cab >= kCabs) return false;
    fake_ip(cab, out, n);
    return true;
}

bool GvrLinking::GetInvalidIP(char* ip) { return ip && strcmp(ip, "127.0.0.1") == 0; }

bool GvrLinking::GetLocalIP(char* out, int n)
{
    if (!out || n < 8) return false;
    fake_ip(GetCabIndex(), out, n);
    return true;
}

bool GvrLinking::GetMacAddress(char* out, int n)
{
    if (!out || n < 13) return false;
    fake_mac(GetCabIndex(), out, n);
    return true;
}

bool GvrLinking::GetMacAddress(char* out, int n, int cab)
{
    if (!out || n < 13 || cab < 0 || cab >= kCabs) return false;
    fake_mac(cab, out, n);
    return true;
}

unsigned long GvrLinking::GetNextSession() { return 1; }
void GvrLinking::SetSession(unsigned long) {}

int GvrLinking::GetNumCabinetsOnline()
{
    int c = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < kCabs; i++) if (g_cab[i].online) c++;
    LeaveCriticalSection(&g_cs);
    return c;
}

sGvrPacket* GvrLinking::GetPacket(int cab) { return (cab < 0 || cab >= kCabs) ? NULL : &g_cab[cab].pkt; }

unsigned long GvrLinking::GetPacketsReceived()
{
    EnterCriticalSection(&g_cs); unsigned long r = g_received; LeaveCriticalSection(&g_cs);
    return r;
}

bool GvrLinking::IsOnline(int cab)
{
    if (cab < 0 || cab >= kCabs) return false;
    EnterCriticalSection(&g_cs); bool r = g_cab[cab].online; LeaveCriticalSection(&g_cs);
    return r;
}

bool GvrLinking::IsOnline(char* status, int n)
{
    bool on = IsOnline(GetCabIndex()) && g_self >= 0;
    if (status && n > 0) { strncpy(status, on ? "Online" : "Offline", n); status[n - 1] = 0; }
    return on;
}

bool GvrLinking::SetCabinetNumber(int number)
{
    if (number >= 10) return false;
    EnterCriticalSection(&g_cs); g_cabNumber = number; LeaveCriticalSection(&g_cs);
    return true;
}

static SOCKET g_relay = INVALID_SOCKET;
static sockaddr_in g_relayAddr;

static unsigned long g_blocksSent, g_blocksDropped;
static DWORD g_lastBlockLog;

void GvrLinking::SetPacketDataBlock(unsigned long size, void* data)
{
    if (!size || !data || size > 4000) return;
    EnterCriticalSection(&g_cs);
    bool linked = g_self >= 0 && g_relay != INVALID_SOCKET && !g_suspended;
    if (linked) g_blocksSent++; else g_blocksDropped++;
    DWORD now = GetTickCount();
    if (g_logPath[0] && (g_blocksSent + g_blocksDropped <= 3 || now - g_lastBlockLog > 10000)) {
        g_lastBlockLog = now;
        logf("menus send #%lu (%s, %lu not sent while offline/paused): %.200s", g_blocksSent + g_blocksDropped,
             linked ? "sent" : "NOT sent", g_blocksDropped, (const char*)data);
    }
    if (linked) {
        // to the others through the relay (UDP send: never blocks)
        unsigned char buf[4100];
        memcpy(buf, kMagic, 4); buf[4] = T_LINK_BCAST;
        memcpy(buf + 5, data, size);
        sendto(g_relay, (const char*)buf, (int)size + 5, 0, (sockaddr*)&g_relayAddr, sizeof(g_relayAddr));
        // and back to ourselves, handed over by the network thread like the original's echo
        int next = (g_ownTail + 1) % 64;
        if (next != g_ownHead) {
            g_own[g_ownTail].data = (char*)malloc(size);
            if (g_own[g_ownTail].data) {
                memcpy(g_own[g_ownTail].data, data, size);
                g_own[g_ownTail].len = (int)size;
                g_ownTail = next;
            }
        }
    }
    LeaveCriticalSection(&g_cs);
}

void GvrLinking::SuspendPackets(bool suspend) { g_suspended = suspend; }   // the original waited here; never wait

int  GvrLinking::NetIPXSocket(unsigned short) { return 0; }
void GvrLinking::NetadrToSockadr(unsigned short, sockaddr_ipx*, unsigned int) {}

// ---------------------------------------------------------------- settings

static void ini_str(const char* ini, const char* sec, const char* key, char* out, int n)
{
    GetPrivateProfileStringA(sec, key, "", out, n, ini);
    char* e = out + strlen(out);
    while (e > out && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
}

// ---------------------------------------------------------------- network thread

static SOCKET udp_bound(unsigned long ip, unsigned short port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return s;
    sockaddr_in a = { 0 };
    a.sin_family = AF_INET; a.sin_addr.s_addr = ip; a.sin_port = htons(port);
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); return INVALID_SOCKET; }
    BOOL off = FALSE; DWORD nb = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), NULL, 0, &nb, NULL, NULL);
    return s;
}

static SOCKET g_listen[kCabs];                     // joining side: 127.10.0.<s+1>:22544 for server s
static sockaddr_in g_raceAddr[kCabs];              // our race, as seen on listener s
static bool g_haveRace[kCabs];
static SOCKET g_serve[kCabs];                      // server side: 127.0.0.<m+1> for remote player m
static unsigned long g_up, g_down;

static unsigned long g_fromCab[kCabs];
static DWORD g_lastRecvLog;

static void deliver(int cab, const char* data, int len)
{
    // fill the table entry the plug-in reads, then call it without holding our lock
    EnterCriticalSection(&g_cs);
    if (g_suspended || !g_cb || cab < 0 || cab >= kCabs) { LeaveCriticalSection(&g_cs); return; }
    g_fromCab[cab]++;
    DWORD now = GetTickCount();
    if (g_logPath[0] && (g_fromCab[cab] <= 2 || now - g_lastRecvLog > 10000)) {
        g_lastRecvLog = now;
        char counts[160] = ""; int o = 0;
        for (int i = 0; i < kCabs; i++) if (g_fromCab[i]) o += _snprintf(counts + o, sizeof counts - o, " cab%d=%lu", i + 1, g_fromCab[i]);
        logf("menus receive from cabinet %d (totals:%s): %.200s", cab + 1, counts, data);
    }
    sGvrPacket* p = &g_cab[cab].pkt;
    p->tick = GetTickCount();
    fake_mac(cab, p->mac, sizeof(p->mac));
    fake_ip(cab, p->ip, sizeof(p->ip));
    p->cabinet = cab + 1;
    p->length = len;
    p->data = (void*)data;
    g_received++;
    void* (__cdecl* cb)(int, sGvrPacket*) = g_cb;
    LeaveCriticalSection(&g_cs);
    cb(cab, p);
    EnterCriticalSection(&g_cs);
    p->data = NULL;
    LeaveCriticalSection(&g_cs);
}

static void on_status(int self, unsigned mask)
{
    EnterCriticalSection(&g_cs);
    bool changed = g_self != self;
    if (changed) logf("link: we are cabinet %d", self + 1);
    g_self = self;
    for (int i = 0; i < kCabs; i++) {
        bool on = (mask >> i) & 1;
        if (on != g_cab[i].online) logf("link: cabinet %d %s", i + 1, on ? "joined" : "left");
        g_cab[i].online = on;
    }
    LeaveCriticalSection(&g_cs);
    if (changed) {
        // Our race reaches server s on 127.<10+self>.0.<s+1>: a range per cabinet, so several menus on ONE PC
        // (tests) never want the same address. The race learns "self" from this variable (it is our child
        // process) and the GvrIO shim writes the matching address into its -client switch.
        char v[8]; _snprintf(v, sizeof v, "%d", self); v[sizeof v - 1] = 0;
        SetEnvironmentVariableA("GVRLINK_SELF", v);
        for (int i = 0; i < kCabs; i++)
            if (g_listen[i] != INVALID_SOCKET) { closesocket(g_listen[i]); g_listen[i] = INVALID_SOCKET; g_haveRace[i] = false; }
    }
    // a listener for every other member: our race may be told to join any of them
    for (int i = 0; i < kCabs; i++) {
        bool want = ((mask >> i) & 1) && i != self;
        if (want && g_listen[i] == INVALID_SOCKET) {
            g_listen[i] = udp_bound(htonl(0x7F000000 | ((10 + self) << 16) | (i + 1)), kGamePort);   // 127.<10+self>.0.<i+1>
            if (g_listen[i] == INVALID_SOCKET) logf("link: cannot listen on 127.%d.0.%d:%u (%d)", 10 + self, i + 1, kGamePort, WSAGetLastError());
        } else if (!want && g_listen[i] != INVALID_SOCKET) {
            closesocket(g_listen[i]); g_listen[i] = INVALID_SOCKET; g_haveRace[i] = false;
        }
    }
}

static void set_offline(const char* why)
{
    EnterCriticalSection(&g_cs);
    bool was = g_self >= 0;
    for (int i = 0; i < kCabs; i++) g_cab[i].online = false;
    g_self = -1;
    LeaveCriticalSection(&g_cs);
    if (was) logf("link: offline (%s)", why);
}

static void send_data(int dst, int dir, const char* payload, int n)
{
    static unsigned char buf[8200];
    if (n > 8000) return;
    memcpy(buf, kMagic, 4); buf[4] = T_LINK_DATA; buf[5] = (unsigned char)dst; buf[6] = (unsigned char)dir;
    memcpy(buf + 7, payload, n);
    sendto(g_relay, (const char*)buf, n + 7, 0, (sockaddr*)&g_relayAddr, sizeof(g_relayAddr));
    g_up++;
}

static DWORD WINAPI net_thread(LPVOID)
{
    char ini[MAX_PATH], mode[32], relay[256], room[64];
    _snprintf(ini, sizeof ini, "%s\\nascar_settings.ini", g_root); ini[sizeof ini - 1] = 0;
    ini_str(ini, "Multiplayer", "Mode", mode, sizeof mode);
    ini_str(ini, "Multiplayer", "Relay", relay, sizeof relay);
    ini_str(ini, "Multiplayer", "Room", room, sizeof room);
    char logOn[16]; ini_str(ini, "Debug", "Log", logOn, sizeof logOn);
    if (!_stricmp(logOn, "true") || !strcmp(logOn, "1")) {
        _snprintf(g_logPath, sizeof g_logPath, "%s\\LOG\\gvrlinking-%lu.log", g_root, GetCurrentProcessId());
        g_logPath[sizeof g_logPath - 1] = 0;
    }
    logf("--- GvrLinking (relay link for the menus) - Mode=%s Relay=%s Room=%s", mode, relay, room);
    if (_stricmp(mode, "lobby") != 0 || !relay[0]) {
        logf("link: not used (needs [Multiplayer] Mode=lobby and Relay) - the menus stay single-cabinet");
        return 0;
    }
    char roomKey[kRoomLen] = { 0 };
    int rn = 0;
    for (char* c = room; *c && rn < kRoomLen - 1; c++) {
        char ch = (char)tolower((unsigned char)*c);
        if (isalnum((unsigned char)ch) || ch == '-' || ch == '_') roomKey[rn++] = ch;
    }
    if (!rn) strcpy(roomKey, "public");          // no Room = the shared public lobby
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { logf("link: WSAStartup failed"); return 0; }
    char host[256]; strcpy(host, relay);
    char port[16]; _snprintf(port, sizeof port, "%u", kDefaultRelayPort);
    char* colon = strrchr(host, ':');
    if (colon) { *colon = 0; strncpy(port, colon + 1, sizeof port - 1); }
    for (int i = 0; i < kCabs; i++) { g_listen[i] = INVALID_SOCKET; g_serve[i] = INVALID_SOCKET; }

    SOCKET rs = INVALID_SOCKET;
    DWORD lastHello = 0, lastStatus = 0, lastResolve = 0;
    bool resolved = false;
    static char buf[8200];
    while (!g_stop) {
        DWORD now = GetTickCount();
        if (!resolved && (lastResolve == 0 || now - lastResolve > 10000)) {
            lastResolve = now;
            addrinfo hints = { 0 }, *res = NULL;
            hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
            if (getaddrinfo(host, port, &hints, &res) == 0 && res) {
                EnterCriticalSection(&g_cs);
                g_relayAddr = *(sockaddr_in*)res->ai_addr;
                if (rs == INVALID_SOCKET) rs = udp_bound(INADDR_ANY, 0);
                g_relay = rs;
                LeaveCriticalSection(&g_cs);
                freeaddrinfo(res);
                resolved = rs != INVALID_SOCKET;
                char ip[32]; inet_ntop(AF_INET, &g_relayAddr.sin_addr, ip, sizeof ip);
                logf("link: relay %s = %s:%u, room '%s'", relay, ip, ntohs(g_relayAddr.sin_port), roomKey);
            } else {
                logf("link: cannot find the relay server '%s' (retrying every 10 s)", relay);
            }
        }
        if (!resolved) { Sleep(200); continue; }
        if (now - lastHello >= 1000) {
            unsigned char h[kHelloLen] = { 0 };
            memcpy(h, kMagic, 4); h[4] = T_LINK_HELLO; h[5] = kVersion;
            memcpy(h + 6, roomKey, strlen(roomKey));
            sendto(rs, (const char*)h, kHelloLen, 0, (sockaddr*)&g_relayAddr, sizeof(g_relayAddr));
            lastHello = now;
        }
        if (lastStatus && now - lastStatus > 5000) { set_offline("no answer from the relay"); lastStatus = 0; }

        // our own text blocks back to the menus
        for (;;) {
            EnterCriticalSection(&g_cs);
            if (g_ownHead == g_ownTail) { LeaveCriticalSection(&g_cs); break; }
            Block b = g_own[g_ownHead]; g_ownHead = (g_ownHead + 1) % 64;
            int self = g_self;
            LeaveCriticalSection(&g_cs);
            deliver(self, b.data, b.len);
            free(b.data);
        }

        fd_set rd; FD_ZERO(&rd);
        FD_SET(rs, &rd);
        for (int i = 0; i < kCabs; i++) {
            if (g_listen[i] != INVALID_SOCKET) FD_SET(g_listen[i], &rd);
            if (g_serve[i] != INVALID_SOCKET) FD_SET(g_serve[i], &rd);
        }
        timeval tv = { 0, 10 * 1000 };
        if (select(0, &rd, NULL, NULL, &tv) <= 0) continue;

        if (FD_ISSET(rs, &rd)) {
            int n = recvfrom(rs, buf, sizeof buf, 0, NULL, NULL);
            if (n >= 5 && memcmp(buf, kMagic, 4) == 0) {
                unsigned char t = (unsigned char)buf[4];
                if (t == T_LINK_STATUS && n >= 8) {
                    lastStatus = now;
                    on_status((unsigned char)buf[5], (unsigned char)buf[6] | ((unsigned char)buf[7] << 8));
                } else if (t == T_LINK_BCAST && n >= 6) {
                    // copy: deliver() hands the plug-in a pointer that must stay valid during its call
                    int len = n - 6;
                    char* copy = (char*)malloc(len + 1);
                    if (copy) { memcpy(copy, buf + 6, len); copy[len] = 0; deliver((unsigned char)buf[5], copy, len); free(copy); }
                } else if (t == T_LINK_DATA && n >= 7) {
                    int src = (unsigned char)buf[5], dir = (unsigned char)buf[6];
                    if (src < kCabs) {
                        g_down++;
                        if (dir == DIR_TO_SERVER) {            // a remote player's race -> our race (we are its server)
                            if (g_serve[src] == INVALID_SOCKET) {
                                g_serve[src] = udp_bound(htonl(0x7F000001 + src + 1), 0);   // 127.0.0.<src+2>
                                logf("link: race traffic from cabinet %d (we are the server)", src + 1);
                            }
                            sockaddr_in race = { 0 };
                            race.sin_family = AF_INET; race.sin_addr.s_addr = htonl(0x7F0000FE); race.sin_port = htons(kGamePort);
                            if (g_serve[src] != INVALID_SOCKET)
                                sendto(g_serve[src], buf + 7, n - 7, 0, (sockaddr*)&race, sizeof race);
                        } else if (dir == DIR_TO_CLIENT && g_listen[src] != INVALID_SOCKET && g_haveRace[src]) {
                            sendto(g_listen[src], buf + 7, n - 7, 0, (sockaddr*)&g_raceAddr[src], sizeof(sockaddr_in));
                        }
                    }
                } else if (t == T_ERROR && n >= 6) {
                    logf("link: relay refused (code %d)", (unsigned char)buf[5]);
                }
            }
        }
        for (int i = 0; i < kCabs; i++) {
            if (g_listen[i] != INVALID_SOCKET && FD_ISSET(g_listen[i], &rd)) {      // our race -> server i
                sockaddr_in from; int fl = sizeof from;
                int n = recvfrom(g_listen[i], buf, sizeof buf, 0, (sockaddr*)&from, &fl);
                if (n > 0) {
                    if (!g_haveRace[i]) logf("link: our race joins cabinet %d's race", i + 1);
                    g_raceAddr[i] = from; g_haveRace[i] = true;
                    send_data(i, DIR_TO_SERVER, buf, n);
                }
            }
            if (g_serve[i] != INVALID_SOCKET && FD_ISSET(g_serve[i], &rd)) {        // our race (server) -> player i
                int n = recvfrom(g_serve[i], buf, sizeof buf, 0, NULL, NULL);
                if (n > 0) send_data(i, DIR_TO_CLIENT, buf, n);
            }
        }
    }
    if (rs != INVALID_SOCKET) {
        unsigned char bye[5]; memcpy(bye, kMagic, 4); bye[4] = T_BYE;
        sendto(rs, (const char*)bye, 5, 0, (sockaddr*)&g_relayAddr, sizeof(g_relayAddr));
    }
    logf("link: stopped (race packets up %lu, down %lu)", g_up, g_down);
    return 0;
}

bool GvrLinking::SetPacketReceiveCallback(void* (__cdecl* cb)(int, sGvrPacket*))
{
    EnterCriticalSection(&g_cs);
    g_cb = cb;
    bool start = !g_started; g_started = true;
    LeaveCriticalSection(&g_cs);
    if (start) {
        HANDLE h = CreateThread(NULL, 0, net_thread, NULL, 0, NULL);   // not waited on (loader lock safe)
        if (h) CloseHandle(h);
    }
    return true;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        // <root>\Shell\bin\GvrLinking.dll -> <root>
        GetModuleFileNameA(inst, g_root, MAX_PATH);
        for (int up = 0; up < 3; up++) { char* s = strrchr(g_root, '\\'); if (s) *s = 0; }
    }
    return TRUE;
}
