// GvrIOCoin: answers the menus' coin requests in place of the cabinet's GVRCoinMonitor.exe.
// Coin slots are keys or USB buttons from [Coin] in nascar_settings.ini. Off by default.
// Protocol notes: docs/findings/NASCAR_FINDINGS.md, section "Coin monitor".
#include "GvrIOPad.h"
#include <mmsystem.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#pragma comment(lib, "winmm.lib")

static const char kServer[] = "GVRCoinMonitor";
static const int  kMsgSize = 64;         // what both the monitor and its clients default to
static const DWORD kMaxAgeMs = 500;      // the monitor drops requests older than this

enum { MSG_OK = 2008, MSG_ERROR = 2009 };
enum { ST_BAD = 1, ST_NO_SOURCE = 5, ST_BAD_INDEX = 8 };
enum { SLOT_COINS = 1, SLOT_SERVICE = 2 };

// ------------------------------------------------------------------ saved coin state
struct CoinState {
    int coins, service, spentCoins, spentService;
    int input[4];                        // coins per slot since the last counter reset
    int period[5], lifetime[5];          // Team Racing's per-slot counters
    int meter[2];                        // cabinets pulse a mechanical meter, here a number
};
static CoinState g_st;
static bool g_dirty = false;
static bool g_serviceEnabled = true;     // the monitor starts with service coins allowed
static char g_root[MAX_PATH] = "";
static char g_statePath[MAX_PATH] = "";

static void state_load(void)
{
    const char* p = g_statePath;
    g_st.coins        = GetPrivateProfileIntA("Coins", "Coins", 0, p);
    g_st.service      = GetPrivateProfileIntA("Coins", "Service", 0, p);
    g_st.spentCoins   = GetPrivateProfileIntA("Coins", "SpentCoins", 0, p);
    g_st.spentService = GetPrivateProfileIntA("Coins", "SpentService", 0, p);
    char k[32];
    for (int i = 0; i < 4; ++i) { sprintf(k, "Input%d", i); g_st.input[i] = GetPrivateProfileIntA("Counters", k, 0, p); }
    for (int i = 0; i < 5; ++i) {
        sprintf(k, "Period%d", i);   g_st.period[i] = GetPrivateProfileIntA("Counters", k, 0, p);
        sprintf(k, "Lifetime%d", i); g_st.lifetime[i] = GetPrivateProfileIntA("Counters", k, 0, p);
    }
    for (int i = 0; i < 2; ++i) { sprintf(k, "Meter%d", i); g_st.meter[i] = GetPrivateProfileIntA("Counters", k, 0, p); }
}

// Written to a temporary file first, so a power cut never loses the credits.
static void state_save(void)
{
    if (!g_dirty) return;
    g_dirty = false;
    char tmp[MAX_PATH];
    _snprintf(tmp, sizeof tmp, "%s.tmp", g_statePath); tmp[sizeof tmp - 1] = 0;
    FILE* f = fopen(tmp, "w");
    if (!f) { shim_log("coin: cannot write %s", tmp); return; }
    fprintf(f, "; Coin credits and counters. The game rewrites this file.\n[Coins]\n");
    fprintf(f, "Coins=%d\nService=%d\nSpentCoins=%d\nSpentService=%d\n[Counters]\n",
            g_st.coins, g_st.service, g_st.spentCoins, g_st.spentService);
    for (int i = 0; i < 4; ++i) fprintf(f, "Input%d=%d\n", i, g_st.input[i]);
    for (int i = 0; i < 5; ++i) fprintf(f, "Period%d=%d\nLifetime%d=%d\n", i, g_st.period[i], i, g_st.lifetime[i]);
    for (int i = 0; i < 2; ++i) fprintf(f, "Meter%d=%d\n", i, g_st.meter[i]);
    fflush(f);
    fclose(f);
    if (!MoveFileExA(tmp, g_statePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        shim_log("coin: cannot replace %s (error %lu)", g_statePath, GetLastError());
}

// ------------------------------------------------------------------ the coin rules
static int  mask_of(int v) { return (v <= 0 || v >= 255) ? 255 : (v & 255); }
static int  count_of(int mask) { return ((mask & SLOT_SERVICE) ? g_st.service : 0) + ((mask & SLOT_COINS) ? g_st.coins : 0); }

// Service credits are spent before paid coins, like the original.
static int deduct(int mask, int amount)
{
    if (amount < 0 || count_of(mask) < amount) return ST_BAD;
    if (mask & SLOT_SERVICE) {
        int n = g_st.service < amount ? g_st.service : amount;
        g_st.service -= n; g_st.spentService += n; amount -= n;
    }
    if (mask & SLOT_COINS) { g_st.coins -= amount; g_st.spentCoins += amount; }
    g_dirty = true;
    return 0;
}
static int add(int mask, int amount)
{
    if (amount < 0) return ST_BAD;
    if (mask & SLOT_SERVICE) { g_st.service += amount; g_dirty = true; return 0; }
    if (mask & SLOT_COINS)   { g_st.coins   += amount; g_dirty = true; return 0; }
    return ST_NO_SOURCE;
}
static int take_all(int mask)
{
    int n = count_of(mask);
    if (mask & SLOT_SERVICE) g_st.service = 0;
    if (mask & SLOT_COINS)   g_st.coins = 0;
    g_dirty = true;
    return n;
}
static int spent(int mask, bool clear)
{
    int n = ((mask & SLOT_COINS) ? g_st.spentCoins : 0) + ((mask & SLOT_SERVICE) ? g_st.spentService : 0);
    if (clear) {
        if (mask & SLOT_COINS)   g_st.spentCoins = 0;
        if (mask & SLOT_SERVICE) g_st.spentService = 0;
        g_dirty = true;
    }
    return n;
}

// ------------------------------------------------------------------ shared-memory mailboxes
struct Box { HANDLE mtx, evt, map; DWORD* h; };

static bool lock_box(const Box& b)
{
    DWORD r = WaitForSingleObject(b.mtx, 1000);
    return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
}

// A list of connected programs (slots == false) or a queue of ten messages (slots == true).
static bool box_open(Box& b, const char* name, int msgSize, bool slots)
{
    char n[128];
    memset(&b, 0, sizeof b);
    _snprintf(n, sizeof n, "%s_mutex", name); n[sizeof n - 1] = 0;
    b.mtx = CreateMutexA(NULL, FALSE, n);
    _snprintf(n, sizeof n, "%s_event", name); n[sizeof n - 1] = 0;
    b.evt = CreateEventA(NULL, TRUE, FALSE, n);
    if (!b.mtx || !b.evt || !lock_box(b)) return false;
    _snprintf(n, sizeof n, "%s_map", name); n[sizeof n - 1] = 0;
    bool created = false;
    b.map = OpenFileMappingA(FILE_MAP_WRITE, FALSE, n);
    if (!b.map) {
        b.map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, slots ? 10 * msgSize + 128 : 0x198, n);
        created = b.map != NULL;
    }
    if (b.map) b.h = (DWORD*)MapViewOfFile(b.map, FILE_MAP_WRITE, 0, 0, 0);
    if (b.h && created) {
        b.h[0] = msgSize; b.h[1] = 0;
        if (slots) for (int i = 0; i < 10; ++i) { b.h[2 + 3 * i] = 0; b.h[3 + 3 * i] = i * msgSize; b.h[4 + 3 * i] = 0; }
    }
    ReleaseMutex(b.mtx);
    return b.h != NULL;
}
static void box_close(Box& b)
{
    if (b.h) UnmapViewOfFile(b.h);
    if (b.map) CloseHandle(b.map);
    if (b.evt) CloseHandle(b.evt);
    if (b.mtx) CloseHandle(b.mtx);
    memset(&b, 0, sizeof b);
}

// Adds `who` to `name`'s list of connected programs and returns that program's message size.
static int box_register(const char* name, const char* who)
{
    Box b; int size = kMsgSize;
    if (!box_open(b, name, kMsgSize, false)) { box_close(b); return size; }
    if (lock_box(b)) {
        size = (int)b.h[0];
        DWORD n = b.h[1]; bool found = false;
        for (DWORD i = 0; i < n && i < 20; ++i)
            if (strcmp((char*)b.h + 8 + 20 * i, who) == 0) found = true;
        if (!found && n < 20) {
            char* e = (char*)b.h + 8 + 20 * n;
            strncpy(e, who, 19); e[19] = 0;
            b.h[1] = n + 1;
            SetEvent(b.evt);
        }
        ReleaseMutex(b.mtx);
    }
    box_close(b);
    return size;
}

struct Client { char name[21]; Box in, out; };
struct Server {
    const char* name; Box box; Client clients[20]; int n;
    void (*on_message)(Client&, const int*);
};
// The volume monitor is answered too: the menus read it every few frames and would stall otherwise.
static Server g_servers[2] = { { kServer }, { "GVRVolumeMonitor" } };

static void client_add(Server& s, const char* name)
{
    for (int i = 0; i < s.n; ++i) if (strcmp(s.clients[i].name, name) == 0) return;
    if (s.n >= 20 || !*name) return;
    Client& c = s.clients[s.n];
    memset(&c, 0, sizeof c);
    lstrcpynA(c.name, name, sizeof c.name);
    char n[64];
    _snprintf(n, sizeof n, "%s_to_%s", name, s.name); n[sizeof n - 1] = 0;
    bool ok = box_open(c.in, n, (int)s.box.h[0], true);
    int theirSize = box_register(name, s.name);
    _snprintf(n, sizeof n, "%s_to_%s", s.name, name); n[sizeof n - 1] = 0;
    ok = box_open(c.out, n, theirSize, true) && ok;
    if (!ok) { box_close(c.in); box_close(c.out); shim_log("coin: %s cannot connect to %s", s.name, name); return; }
    ++s.n;
    shim_log("coin: %s connected to %s", name, s.name);
}

static void clients_scan(Server& s)
{
    char names[20][21]; int n = 0;
    if (!lock_box(s.box)) return;
    for (DWORD i = 0; i < s.box.h[1] && i < 20; ++i) {
        lstrcpynA(names[n], (char*)s.box.h + 8 + 20 * i, 20);
        ++n;
    }
    ResetEvent(s.box.evt);
    ReleaseMutex(s.box.mtx);
    for (int i = 0; i < n; ++i) client_add(s, names[i]);
}

static void reply(Client& c, int type, int value)
{
    int m[4] = { 0, type, 4, value };
    if (!lock_box(c.out)) return;
    DWORD* h = c.out.h; DWORD n = h[1];
    if (n < 10 && sizeof m < h[0]) {
        memcpy((BYTE*)h + 128 + h[3 + 3 * n], m, sizeof m);
        h[2 + 3 * n] = sizeof m;
        h[4 + 3 * n] = GetTickCount();
        h[1] = n + 1;
        SetEvent(c.out.evt);
    }
    ReleaseMutex(c.out.mtx);
}
static void answer(Client& c, int status, int value)
{
    if (status) reply(c, MSG_ERROR, status); else reply(c, MSG_OK, value);
}

// Always 0, the value the menus already get today. Windows keeps controlling the real volume.
static void handle_volume(Client& c, const int* m)
{
    if (m[1] == 2010) reply(c, MSG_OK, 0);
}

static void handle_coin(Client& c, const int* m)
{
    int a = m[3], b = m[4];
    if (m[1] != 2010 && m[1] != 2023)                  // the menus poll those two all the time
        shim_log("coin: command %d (%d, %d) from %s, credits %d + %d", m[1], a, b, c.name, g_st.coins, g_st.service);
    switch (m[1]) {
    case 2004: answer(c, deduct(255, a), a); break;
    case 2005: answer(c, 0, take_all(255)); break;
    case 2006: if (b >= 0 && b < 2) { g_st.meter[b] += a; g_dirty = true; } reply(c, MSG_OK, a); break;
    case 2010: answer(c, 0, count_of(255)); break;
    case 2021: answer(c, deduct(mask_of(a), b), b); break;
    case 2022: answer(c, 0, take_all(mask_of(a))); break;
    case 2023: answer(c, 0, count_of(mask_of(a))); break;
    case 2024: answer(c, add(mask_of(a), b), b); break;
    case 2032: answer(c, 0, spent(mask_of(a), true)); break;
    case 2033: answer(c, 0, spent(mask_of(a), false)); break;
    case 2040: g_serviceEnabled = a != 0; break;       // the only command with no answer
    case 2041: if (a >= 0 && a < 4) answer(c, 0, g_st.input[a]); else answer(c, ST_BAD_INDEX, 0); break;
    case 2042: answer(c, 0, 0); break;                 // board status: there is no board
    case 2043: if (a >= 0 && a <= 4) answer(c, 0, g_st.period[a]); else answer(c, ST_BAD_INDEX, 0); break;
    case 2044: if (a >= 0 && a <= 4) { g_st.period[a] = 0; g_dirty = true; } answer(c, 0, 0); break;
    case 2045: if (a >= 0 && a <= 4) answer(c, 0, g_st.lifetime[a]); else answer(c, ST_BAD_INDEX, 0); break;
    default: shim_log("coin: unknown command %d from %s", m[1], c.name); break;
    }
}

static void client_pump(Server& s, Client& c)
{
    int msgs[10][16]; int n = 0;
    if (!lock_box(c.in)) return;
    DWORD* h = c.in.h; DWORD now = GetTickCount();
    for (DWORD i = 0; i < h[1] && i < 10; ++i) {
        DWORD len = h[2 + 3 * i], off = h[3 + 3 * i], t = h[4 + 3 * i];
        if (len < 12 || len > h[0] || off + len > 10 * h[0]) continue;
        if (now - t >= kMaxAgeMs && t - now >= kMaxAgeMs) continue;
        memset(msgs[n], 0, sizeof msgs[n]);
        memcpy(msgs[n], (BYTE*)h + 128 + off, len < sizeof msgs[n] ? len : sizeof msgs[n]);
        ++n;
    }
    h[1] = 0;
    ResetEvent(c.in.evt);
    ReleaseMutex(c.in.mtx);
    for (int i = 0; i < n; ++i) s.on_message(c, msgs[i]);
}

// ------------------------------------------------------------------ coin slots: keys and USB buttons
struct Input { const char* setting; int kind; int vk; UINT joy; DWORD bit; bool held; DWORD lastDown; };
enum { IN_NONE, IN_KEY, IN_JOY };
static Input g_inputs[3] = { { "Coin1" }, { "Coin2" }, { "Service" } };
static DWORD g_debounceMs = 30;

static const struct { const char* name; int vk; } KEY_NAMES[] = {
    {"lctrl",VK_LCONTROL},{"rctrl",VK_RCONTROL},{"lshift",VK_LSHIFT},{"rshift",VK_RSHIFT},
    {"lalt",VK_LMENU},{"ralt",VK_RMENU},{"space",VK_SPACE},{"enter",VK_RETURN},{"tab",VK_TAB},
    {"insert",VK_INSERT},{"delete",VK_DELETE},{"home",VK_HOME},{"end",VK_END},
    {"pageup",VK_PRIOR},{"pagedown",VK_NEXT},{"up",VK_UP},{"down",VK_DOWN},{"left",VK_LEFT},{"right",VK_RIGHT},
};

// "5", "c", "f9", "lctrl" or "joy1:3" (first USB controller, button 3). "none" = off.
static void input_parse(Input& in, const char* text)
{
    in.kind = IN_NONE;
    int joy, btn;
    if (!_stricmp(text, "none") || !*text) return;
    if (sscanf(text, "joy%d:%d", &joy, &btn) == 2 || sscanf(text, "JOY%d:%d", &joy, &btn) == 2) {
        if (joy >= 1 && joy <= 16 && btn >= 1 && btn <= 32) { in.kind = IN_JOY; in.joy = joy - 1; in.bit = 1u << (btn - 1); return; }
    } else if (!text[1] && isalnum((unsigned char)text[0])) {
        in.kind = IN_KEY; in.vk = toupper((unsigned char)text[0]); return;
    } else if ((text[0] == 'f' || text[0] == 'F') && atoi(text + 1) >= 1 && atoi(text + 1) <= 24) {
        in.kind = IN_KEY; in.vk = VK_F1 + atoi(text + 1) - 1; return;
    } else {
        for (size_t i = 0; i < sizeof KEY_NAMES / sizeof KEY_NAMES[0]; ++i)
            if (!_stricmp(KEY_NAMES[i].name, text)) { in.kind = IN_KEY; in.vk = KEY_NAMES[i].vk; return; }
    }
    shim_log("coin: [Coin] %s = '%s' is not a key or button - ignored", in.setting, text);
}

// True once per press. A coin switch bounces, so the release must last the debounce time.
static bool input_edge(Input& in, bool down, DWORD now)
{
    if (down) {
        in.lastDown = now;
        if (!in.held) { in.held = true; return true; }
    } else if (in.held && now - in.lastDown >= g_debounceMs) {
        in.held = false;
    }
    return false;
}

static bool input_down(const Input& in)
{
    if (in.kind == IN_KEY) return (GetAsyncKeyState(in.vk) & 0x8001) != 0;  // low bit: a press shorter than one poll
    if (in.kind == IN_JOY) {
        static DWORD s_retry[16];
        DWORD now = GetTickCount();
        if (s_retry[in.joy] && now - s_retry[in.joy] < 2000) return false;
        JOYINFOEX ji; memset(&ji, 0, sizeof ji);
        ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetPosEx(in.joy, &ji) != JOYERR_NOERROR) { s_retry[in.joy] = now ? now : 1; return false; }
        s_retry[in.joy] = 0;
        return (ji.dwButtons & in.bit) != 0;
    }
    return false;
}

// Coins count only while the menus or the race are in front, so typing a 5 in chat adds nothing.
static bool game_in_front(void)
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return true;
    typedef BOOL (WINAPI* QueryPath)(HANDLE, DWORD, LPSTR, PDWORD);
    QueryPath query = (QueryPath)GetProcAddress(GetModuleHandleA("kernel32.dll"), "QueryFullProcessImageNameA");
    if (!query) return false;
    HANDLE p = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, FALSE, pid);
    if (!p) return false;
    char path[MAX_PATH]; DWORD n = MAX_PATH;
    BOOL ok = query(p, 0, path, &n);
    CloseHandle(p);
    size_t len = strlen(g_root);
    return ok && len && _strnicmp(path, g_root, len) == 0 && path[len] == '\\';
}

static void coin_inserted(int slot)
{
    if (slot == 2) {
        if (!g_serviceEnabled) { shim_log("coin: service button ignored (service coins are off)"); return; }
        g_st.service += 1;
    } else {
        g_st.coins += 1;
        g_st.input[slot] += 1; g_st.period[slot] += 1; g_st.lifetime[slot] += 1;
    }
    g_dirty = true;
    shim_log("coin: %s pressed, credits now %d coins + %d service", g_inputs[slot].setting, g_st.coins, g_st.service);
}

static void inputs_poll(void)
{
    DWORD now = GetTickCount();
    for (int i = 0; i < 3; ++i) {
        if (g_inputs[i].kind == IN_NONE) continue;
        if (input_edge(g_inputs[i], input_down(g_inputs[i]), now) && game_in_front()) coin_inserted(i);
    }
}

// ------------------------------------------------------------------ the menus' instant-timeout patches
// The installer sets the coin wait to zero because no monitor runs. Coin mode needs the real wait.
static const struct WaitPatch { const char* dll; DWORD rva; BYTE patched[4]; BYTE original[4]; int n; } WAITS[] = {
    { "HerculesPlugIn.dll", 0x4904,  { 0x33, 0xC0, 0x90 },       { 0x8B, 0x45, 0x0C },       3 },
    { "NASCARPlugIn.dll",   0x2D0AC, { 0x33, 0xC9, 0x90, 0x90 }, { 0x8B, 0x4C, 0x24, 0x08 }, 4 },
    { "NASCARPlugIn.dll",   0x3251C, { 0x33, 0xC9, 0x90, 0x90 }, { 0x8B, 0x4C, 0x24, 0x08 }, 4 },
};
static bool g_waitDone[sizeof WAITS / sizeof WAITS[0]];

static void waits_restore(void)
{
    for (size_t i = 0; i < sizeof WAITS / sizeof WAITS[0]; ++i) {
        if (g_waitDone[i]) continue;
        HMODULE m = GetModuleHandleA(WAITS[i].dll);
        if (!m) continue;
        g_waitDone[i] = true;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)m + ((IMAGE_DOS_HEADER*)m)->e_lfanew);
        if (WAITS[i].rva + 8 > nt->OptionalHeader.SizeOfImage) continue;
        BYTE* p = (BYTE*)m + WAITS[i].rva;
        if (memcmp(p, WAITS[i].patched, WAITS[i].n) != 0) continue;     // the other game's build
        DWORD old;
        if (!VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old)) { shim_log("coin: cannot restore wait in %s", WAITS[i].dll); continue; }
        DWORD v = *(DWORD*)p;                                           // one aligned write: the game is running
        memcpy(&v, WAITS[i].original, WAITS[i].n);
        InterlockedExchange((LONG*)p, (LONG)v);
        VirtualProtect(p, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 4);
        shim_log("coin: reply wait restored in %s", WAITS[i].dll);
    }
}

// ------------------------------------------------------------------ start-up and main loop
static bool real_monitor_running(void)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 e; e.dwSize = sizeof e;
    bool found = false;
    for (BOOL ok = Process32First(s, &e); ok && !found; ok = Process32Next(s, &e))
        found = _stricmp(e.szExeFile, "GVRCoinMonitor.exe") == 0;
    CloseHandle(s);
    return found;
}

static void setting(const char* ini, const char* key, const char* def, char* out, int n)
{
    GetPrivateProfileStringA("Coin", key, def, out, n, ini);
    char* c = strpbrk(out, ";#");
    if (c) *c = 0;
    char* e = out + strlen(out);
    while (e > out && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
}

static bool coin_setup(void)
{
    char ini[MAX_PATH], v[64];
    if (!shim_settings_path(ini, sizeof ini)) return false;
    setting(ini, "Enabled", "false", v, sizeof v);
    if (_stricmp(v, "true") && _stricmp(v, "yes") && _stricmp(v, "on") && strcmp(v, "1")) return false;
    if (real_monitor_running()) { shim_log("coin: the cabinet's own coin monitor is running - staying off"); return false; }

    lstrcpynA(g_root, ini, sizeof g_root);
    char* s = strrchr(g_root, '\\');
    if (s) *s = 0;
    _snprintf(g_statePath, sizeof g_statePath, "%s\\Game\\Save\\nascar_coins.ini", g_root);
    g_statePath[sizeof g_statePath - 1] = 0;
    state_load();

    static const char* const defaults[3] = { "5", "6", "7" };
    for (int i = 0; i < 3; ++i) {
        setting(ini, g_inputs[i].setting, defaults[i], v, sizeof v);
        input_parse(g_inputs[i], v);
    }
    setting(ini, "Debounce", "30", v, sizeof v);
    int d = atoi(v);
    g_debounceMs = d < 5 ? 5 : d > 1000 ? 1000 : d;

    g_servers[0].on_message = handle_coin;
    g_servers[1].on_message = handle_volume;
    for (int i = 0; i < 2; ++i)
        if (!box_open(g_servers[i].box, g_servers[i].name, kMsgSize, false)) {
            shim_log("coin: cannot open the %s mailbox", g_servers[i].name);
            return false;
        }
    shim_log("coin: on. %d coins + %d service saved in %s", g_st.coins, g_st.service, g_statePath);
    return true;
}

static DWORD WINAPI coin_thread(LPVOID)
{
    if (!coin_setup()) return 0;
    DWORD lastWaitCheck = 0;
    for (;;) {
        HANDLE ev[42]; DWORD n = 0;
        for (int k = 0; k < 2; ++k) {
            ev[n++] = g_servers[k].box.evt;
            for (int i = 0; i < g_servers[k].n; ++i) ev[n++] = g_servers[k].clients[i].in.evt;
        }
        WaitForMultipleObjects(n, ev, FALSE, 10);
        DWORD now = GetTickCount();
        bool rescan = now - lastWaitCheck >= 100;
        for (int k = 0; k < 2; ++k) {
            Server& s = g_servers[k];
            if (rescan || WaitForSingleObject(s.box.evt, 0) == WAIT_OBJECT_0) clients_scan(s);
            for (int i = 0; i < s.n; ++i) client_pump(s, s.clients[i]);
        }
        inputs_poll();
        state_save();
        if (rescan) { lastWaitCheck = now; waits_restore(); }
    }
}

void coin_attach(void)
{
    HANDLE t = CreateThread(NULL, 0, coin_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
