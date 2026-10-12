// ============================================================================
//  NascarLaunch.exe - the thing the player actually starts.
//
//  NASCAR Team Racing (GlobalVR), portable install.
//
//  It has little to patch, because this game is configurable:
//
//    * NASCAR reads FullScreenWidth / FullScreenHeight from the registry and also accepts -width/-height/
//      -windowed/-fullscreen on the command line, so NOTHING is patched here.
//
//  What it does:
//    1. reads nascar_settings.ini next to itself
//    2. writes it into the game's PRIVATE registry, <install>\nascar_registry.ini
//       (the GvrIO shim answers the game's registry calls from that file), including
//       the "cmd" value - the argument string the SHELL passes to the race when
//       the player presses start. That is how a setting in the ini reaches a
//       game the shell launches on its own. No administrator rights involved.
//    3. starts the cabinet: AMPlayer.exe running Shell.am (attract -> select
//       -> race, the full cabinet experience), with the GvrIO shim loaded before
//       AMPlayer's own code so even its first registry reads are private.
//       Mode=race in the ini skips the shell and goes straight into a session.
//    4. hands the new window the foreground, and passes GVRSQLITE_DB_NAS1
//       down to the child so the SQLite database is found even when Explorer
//       has not been restarted since the install.
//    5. [Display] Borderless=true: the race runs -windowed and, while we wait
//       on the shell, every 250 ms we look for a NASCAR_GVR.exe window, strip
//       its caption/frame and place it over its monitor - fullscreen look,
//       instant alt-tab, desktop resolution untouched (2026-10-04).
//
//  Never modifies any game executable.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <string>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

static char g_root[MAX_PATH];      // install root (folder holding this exe)
static char g_ini[MAX_PATH];       // nascar_settings.ini
static char g_log[MAX_PATH];       // nascarlaunch.log

// ---------------------------------------------------------------- logging

static void logf(const char* fmt, ...)
{
    FILE* f = fopen(g_log, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

static void die(const char* fmt, ...)
{
    char msg[4096];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    logf("FATAL: %s", msg);
    MessageBoxA(NULL, msg, "NASCAR Team Racing", MB_ICONERROR | MB_OK);
    ExitProcess(1);
}

// ---------------------------------------------------------------- PC configuration dump
// The single most useful thing in a "works on my machine, not theirs" report - the GPU driver
// above all. Pure registry + Win32, no WMI/COM, so it runs on everything XP..Win11. Switched on
// by nascar_settings.ini [Debug] Log=true (same flag the shim and the SQLite provider read).
static bool reg_read_sz(HKEY root, const char* sub, const char* val, char* out, DWORD n)
{
    out[0] = 0;
    HKEY k;
    if (RegOpenKeyExA(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, cb = n - 1;
    LONG r = RegQueryValueExA(k, val, NULL, &type, (BYTE*)out, &cb);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS) { out[0] = 0; return false; }
    if (type == REG_DWORD && cb == 4) { DWORD d = *(DWORD*)out; snprintf(out, n, "%lu", d); return true; }
    out[(cb < n) ? cb : n - 1] = 0;   // REG_SZ may omit the terminator
    return true;
}

// .NET Framework 1.1 - the front end needs it (AMPlayer.exe.config pins v1.1.4322 for the
// PLUSDE database layer; the race does not use .NET). Detected by the runtime's FILE:
// a working 1.1 often has no NDP registry key, so the registry would falsely say no.
static bool dotnet11_installed(void)
{
    char p[MAX_PATH];
    UINT n = GetWindowsDirectoryA(p, MAX_PATH);
    if (!n || n > MAX_PATH - 48) return true;          // cannot tell -> do not block
    lstrcatA(p, "\\Microsoft.NET\\Framework\\v1.1.4322\\mscorlib.dll");
    return PathFileExistsA(p) != FALSE;
}

static void log_pc_config(void)
{
    logf("---- PC configuration (diagnostic) ----");

    char prod[256], disp[64], build[32], ubr[32];
    reg_read_sz(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ProductName", prod, sizeof(prod));
    reg_read_sz(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "DisplayVersion", disp, sizeof(disp));
    reg_read_sz(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "CurrentBuildNumber", build, sizeof(build));
    reg_read_sz(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "UBR", ubr, sizeof(ubr));
    logf("os      : %s %s (build %s.%s)", prod, disp, build, ubr[0] ? ubr : "0");

    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    const char* arch = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64" :
                       si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86" :
                       si.wProcessorArchitecture == 12 /*ARM64*/ ? "ARM64" : "?";
    logf("arch    : %s, %lu logical CPUs", arch, si.dwNumberOfProcessors);

    char cpu[256], vendor[64];
    reg_read_sz(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", cpu, sizeof(cpu));
    reg_read_sz(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "VendorIdentifier", vendor, sizeof(vendor));
    logf("cpu     : %s (%s)", cpu[0] ? cpu : "?", vendor);

    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        logf("ram     : %.1f GB total, %.1f GB free",
             ms.ullTotalPhys / 1073741824.0, ms.ullAvailPhys / 1073741824.0);

    char sysman[128], sysprod[128], bbman[128], bbprod[128], bios[128], biosdate[64];
    reg_read_sz(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemManufacturer", sysman, sizeof(sysman));
    reg_read_sz(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemProductName", sysprod, sizeof(sysprod));
    reg_read_sz(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BaseBoardManufacturer", bbman, sizeof(bbman));
    reg_read_sz(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BaseBoardProduct", bbprod, sizeof(bbprod));
    reg_read_sz(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BIOSVersion", bios, sizeof(bios));
    reg_read_sz(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BIOSReleaseDate", biosdate, sizeof(biosdate));
    logf("system  : %s %s", sysman, sysprod);
    logf("mainboard: %s %s", bbman, bbprod);
    logf("bios    : %s (%s)", bios, biosdate);

    // every display adapter + its driver, from the display device-class key
    static const char* CLS = "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
    for (int i = 0; i < 16; ++i) {
        char keypath[320]; snprintf(keypath, sizeof(keypath), "%s\\%04d", CLS, i);
        char desc[256];
        if (!reg_read_sz(HKEY_LOCAL_MACHINE, keypath, "DriverDesc", desc, sizeof(desc)) || !desc[0]) continue;
        char dver[64], ddate[64], prov[128];
        reg_read_sz(HKEY_LOCAL_MACHINE, keypath, "DriverVersion", dver, sizeof(dver));
        reg_read_sz(HKEY_LOCAL_MACHINE, keypath, "DriverDate", ddate, sizeof(ddate));
        reg_read_sz(HKEY_LOCAL_MACHINE, keypath, "ProviderName", prov, sizeof(prov));
        logf("gpu[%d]  : %s  drv %s (%s, %s)", i, desc,
             dver[0] ? dver : "?", ddate[0] ? ddate : "?", prov[0] ? prov : "?");
    }

    DEVMODEA dm; dm.dmSize = sizeof(dm); dm.dmDriverExtra = 0;
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm))
        logf("display : %lux%lu %lubpp @ %luHz (desktop)",
             dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel, dm.dmDisplayFrequency);
    logf(".net 1.1: %s", dotnet11_installed() ? "yes" : "NO (v1.1.4322\\mscorlib.dll missing)");

    logf("---- end PC configuration ----");
}

// ---------------------------------------------------------------- ini

static int ini_int(const char* section, const char* key, int def)
{
    return (int)GetPrivateProfileIntA(section, key, def, g_ini);
}

static std::string ini_str(const char* section, const char* key, const char* def)
{
    char buf[1024];
    GetPrivateProfileStringA(section, key, def, buf, sizeof(buf), g_ini);
    // trim trailing whitespace/comments people leave behind
    std::string s(buf);
    size_t e = s.find_last_not_of(" \t\r\n");
    if (e != std::string::npos) s.erase(e + 1);
    return s;
}

static bool ini_bool(const char* section, const char* key, bool def)
{
    std::string s = ini_str(section, key, def ? "true" : "false");
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]);
    return (s == "1" || s == "true" || s == "yes" || s == "on");
}

// ---------------------------------------------------------------- settings

struct Settings {
    int  width, height;
    bool fullscreen;
    bool borderless;         // windowed race, frame stripped, covering its monitor
    int  musicVolume;
    int  simpleAttract;
    std::string mode;        // "shell" (cabinet) or "race" (straight in)
    std::string extraArgs;
    std::string track, series;
    std::string car;         // kept as text: "01" (Nemechek) and "1" (Truex) are different cars
    int  laps;
    bool waitForExit;
};

static void load_settings(Settings& s)
{
    s.width         = ini_int("Display", "Width", 1280);
    s.height        = ini_int("Display", "Height", 720);
    s.fullscreen    = ini_bool("Display", "Fullscreen", false);
    s.borderless    = ini_bool("Display", "Borderless", false);
    // Borderless is a windowed race whose frame we remove, so it wins over Fullscreen.
    // Width/Height 0 = use the primary screen's size (the usual borderless choice).
    if (s.width  <= 0) s.width  = GetSystemMetrics(SM_CXSCREEN);
    if (s.height <= 0) s.height = GetSystemMetrics(SM_CYSCREEN);
    if (s.borderless) s.fullscreen = false;
    s.musicVolume   = ini_int("Cabinet", "MusicVolume", 70);
    s.simpleAttract = ini_int("Cabinet", "SimpleAttract", 0);
    s.mode          = ini_str("Launcher", "Mode", "shell");
    s.waitForExit   = ini_bool("Launcher", "WaitForExit", true);
    s.extraArgs     = ini_str("Race", "ExtraArgs", "-noabort -notimeout");
    s.track         = ini_str("Race", "Track", "DAYTONA");
    s.series        = ini_str("Race", "Series", "2005NEXTEL");
    // -car names the car's folder (Vehicles\<series>\<make>\<number>), so a leading zero matters.
    // The engine splits its command line on whitespace, so any spaces are dropped.
    std::string car = ini_str("Race", "Car", "10");
    for (size_t i = 0; i < car.size(); i++)
        if (!isspace((unsigned char)car[i])) s.car += car[i];
    if (s.car.empty()) s.car = "10";
    s.laps          = ini_int("Race", "Laps", 3);
    for (size_t i = 0; i < s.mode.size(); i++) s.mode[i] = (char)tolower((unsigned char)s.mode[i]);
}

// The registry "cmd" value is the argument string the SHELL hands the race when
// the player presses start. Appending the display switches here is what makes
// the ini's resolution apply to a game we never launch ourselves.
static std::string compose_cmd(const Settings& s)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "-startPos 43 %s -width %d -height %d %s",
             s.fullscreen ? "-fullscreen" : "-windowed", s.width, s.height, s.extraArgs.c_str());
    return std::string(buf);
}

// The engine knows five series names (2006NEXTEL 2005NEXTEL 2004NEXTEL 2004NNS 2004CTS) but the
// v1.1 disc ships a .gdb for only four - 2006NEXTEL fails the ENTER state and the race exits -1.
// Older releases defaulted to it, and the installer keeps an existing nascar_settings.ini, so check
// the series' file instead of trusting the setting: if it is missing, leave -series out and let the
// game take its own default (2005NEXTEL). Names we cannot map are passed through unchanged.
static bool series_installed(const std::string& series, const char* gameDir)
{
    if (series.empty()) return false;
    const char* n = series.c_str();
    const char* kinds[3][2] = { { "NEXTEL", "NEXTEL Cup" }, { "NNS", "NNS" }, { "CTS", "CTS" } };
    if (strlen(n) > 4 && isdigit((unsigned char)n[0]) && isdigit((unsigned char)n[3])) {
        for (int i = 0; i < 3; i++) {
            if (_stricmp(n + 4, kinds[i][0]) != 0) continue;
            char gdb[MAX_PATH];
            snprintf(gdb, sizeof(gdb), "%s\\GameData\\%.4s %s.gdb", gameDir, n, kinds[i][1]);
            if (PathFileExistsA(gdb)) return true;
            logf("series %s is not on this disc (no %s) - leaving -series out, the game picks its default",
                 n, gdb);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- the game's registry
//
// The game no longer uses the Windows registry for its own keys: the GvrIO shim answers
// HKLM\SOFTWARE\GlobalVR and HKLM\SOFTWARE\gvr from <install>\nascar_registry.ini
// (GvrIOShim\GvrIOReg.cpp; folder values are computed from the install folder there).
// So the launcher just writes the ini-driven values into that file - no administrator
// rights, nothing machine-wide, and no other GVR title's install can collide with it.
static void store_set(const char* name, const std::string& value)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\nascar_registry.ini", g_root);
    std::string q = "\"" + value + "\"";
    WritePrivateProfileStringA("GlobalVR\\NASCAR", name, q.c_str(), path);
}

static void apply_settings(const Settings& s)
{
    char n[32];
    snprintf(n, sizeof(n), "%d", s.width);         store_set("FullScreenWidth", n);
    snprintf(n, sizeof(n), "%d", s.height);        store_set("FullScreenHeight", n);
    snprintf(n, sizeof(n), "%d", s.musicVolume);   store_set("MusicVolume", n);
    snprintf(n, sizeof(n), "%d", s.simpleAttract); store_set("SimpleAttract", n);
    store_set("cmd", compose_cmd(s));
    logf("settings -> nascar_registry.ini: %dx%d %s, cmd=\"%s\"", s.width, s.height,
         s.borderless ? "borderless" : (s.fullscreen ? "fullscreen" : "windowed"), compose_cmd(s).c_str());
}

// Anark Client 3.0 preferences (GvrShell.reg's HKCU part) - per-user, no rights needed.
static void ensure_anark_prefs(void)
{
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Anark\\Client\\3.0\\Preferences", 0, NULL, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    DWORD one = 1, zero = 0, t = 0, n = 0;
    if (RegQueryValueExA(k, "Initialized", NULL, &t, NULL, &n) != ERROR_SUCCESS) {
        RegSetValueExA(k, "Initialized", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
        RegSetValueExA(k, "InstallReturnCode", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
        RegSetValueExA(k, "DisableGL", 0, REG_SZ, (const BYTE*)"1", 2);
        logf("Anark client preferences restored");
    }
    RegCloseKey(k);
}

// ---------------------------------------------------------------- database

// The SQLite provider finds game.db via GVRSQLITE_DB_NAS1. We set it to this install's
// own database; the fallbacks below cover installs made by older installers, which set a
// MACHINE variable. Processes inherit their environment from the Explorer
// session that launched them, so before the first logoff that variable is not
// in our environment - and the shell would sit waiting on a database it cannot
// find. We read it straight from the registry and put it in the child's
// environment, which removes the need to log off at all.
static void ensure_db_env()
{
    // This install's own database always wins, so two installs (or a moved one) never share
    // or swap databases through the machine-wide variable an older installer set.
    {
        char own[MAX_PATH];
        snprintf(own, sizeof(own), "%s\\GVR\\GvrPlus\\game.db", g_root);
        if (PathFileExistsA(own)) {
            SetEnvironmentVariableA("GVRSQLITE_DB_NAS1", own);
            logf("GVRSQLITE_DB_NAS1 = %s (this install)", own);
            return;
        }
    }
    if (GetEnvironmentVariableA("GVRSQLITE_DB_NAS1", NULL, 0) > 0) return;

    HKEY k;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
            0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        char buf[MAX_PATH]; DWORD n = sizeof(buf), type = 0;
        if (RegQueryValueExA(k, "GVRSQLITE_DB_NAS1", NULL, &type, (LPBYTE)buf, &n) == ERROR_SUCCESS) {
            SetEnvironmentVariableA("GVRSQLITE_DB_NAS1", buf);
            logf("GVRSQLITE_DB_NAS1 taken from the registry: %s", buf);
        }
        RegCloseKey(k);
        return;
    }
    // last resort: the database that ships inside this install
    char db[MAX_PATH];
    snprintf(db, sizeof(db), "%s\\GVR\\GvrPlus\\game.db", g_root);
    if (PathFileExistsA(db)) {
        SetEnvironmentVariableA("GVRSQLITE_DB_NAS1", db);
        logf("GVRSQLITE_DB_NAS1 defaulted to %s", db);
    }
}

// ---------------------------------------------------------------- launching

static HWND g_found = NULL;
static DWORD g_wantPid = 0;

static BOOL CALLBACK find_cb(HWND h, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == g_wantPid && IsWindowVisible(h)) { g_found = h; return FALSE; }
    return TRUE;
}

static void give_focus(DWORD pid)
{
    for (int i = 0; i < 100; i++) {          // up to ~10s
        g_found = NULL; g_wantPid = pid;
        EnumWindows(find_cb, 0);
        if (g_found) {
            SetForegroundWindow(g_found);
            SetActiveWindow(g_found);
            logf("foreground handed to window %p", (void*)g_found);
            return;
        }
        Sleep(100);
    }
    logf("no visible window appeared within 10s (the dongle stall does this)");
}


// The cabinet shell can HIDE THE WINDOWS TASKBAR - the same lockdown behaviour as the
// OEM's HideTaskBar.exe. If it exits without restoring it, or is killed, the desktop
// is left with no taskbar at all. Put it back whenever we regain control.
//
// Only taskbars that were VISIBLE BEFORE WE LAUNCHED are ever re-shown. Windows keeps some
// taskbar windows hidden on purpose (the one for a secondary or virtual monitor with
// "show taskbar on all displays" off, e.g. Meta Quest Link's virtual monitor); forcing those
// visible every poll made Explorer hide them again, four times a second - the taskbar
// "freaking out".
static const char* kTrayClasses[2] = { "Shell_TrayWnd", "Shell_SecondaryTrayWnd" };
static HWND g_trays[16];
static int  g_trayRestores[16];
static int  g_trayCount = 0;

static void snapshot_taskbar(void)
{
    int hidden = 0;
    g_trayCount = 0;
    for (int i = 0; i < 2; i++) {
        HWND h = NULL;
        while ((h = FindWindowExA(NULL, h, kTrayClasses[i], NULL)) != NULL) {
            if (!IsWindowVisible(h)) { hidden++; continue; }
            if (g_trayCount < (int)(sizeof(g_trays) / sizeof(g_trays[0]))) {
                g_trayRestores[g_trayCount] = 0;
                g_trays[g_trayCount++] = h;
            }
        }
    }
    logf("taskbar: %d visible window(s) recorded, %d already hidden (left alone)", g_trayCount, hidden);
}

// The shell hides the taskbar once per session, so one restore is normal. A taskbar that is
// hidden again every time we show it belongs to something else - a taskbar replacement such as
// ExplorerPatcher or StartAllBack hides the real one, even on a single monitor.
// After kMaxTrayRestores we stop touching that window instead of fighting it.
static const int kMaxTrayRestores = 3;

static void restore_taskbar(void)
{
    for (int i = 0; i < g_trayCount; i++) {
        HWND h = g_trays[i];
        if (!IsWindow(h) || IsWindowVisible(h) || g_trayRestores[i] > kMaxTrayRestores) continue;
        char cls[64] = "";
        GetClassNameA(h, cls, sizeof(cls));
        if (g_trayRestores[i]++ == kMaxTrayRestores) {
            logf("taskbar: the %s keeps being hidden again (a taskbar tool?) - leaving it alone", cls);
            continue;
        }
        ShowWindow(h, SW_SHOW);
        logf("restored the %s (the shell had hidden it) (%d)", cls, g_trayRestores[i]);
    }
}

// ---------------------------------------------------------------- borderless race
//
// The race is started by the SHELL (AMPlayer.exe), not by us, so we find its window by
// process name. With [Display] Borderless=true the race runs -windowed at Width x Height;
// as soon as its window appears we strip the caption and frame and place it over the
// monitor it is on (centred if it is smaller). The client area then equals the game's
// render size exactly, so the picture is not rescaled. Only NASCAR_GVR.exe is touched,
// never the shell.

static bool pid_is_exe(DWORD pid, const char* exe)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    char path[MAX_PATH]; DWORD n = MAX_PATH;
    bool hit = false;
    if (QueryFullProcessImageNameA(h, 0, path, &n)) {
        const char* b = strrchr(path, '\\');
        hit = _stricmp(b ? b + 1 : path, exe) == 0;
    }
    CloseHandle(h);
    return hit;
}

static BOOL CALLBACK borderless_cb(HWND h, LPARAM lp)
{
    const Settings* s = (const Settings*)lp;
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT rc; GetClientRect(h, &rc);
    if (rc.right < 320 || rc.bottom < 240) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (!pid_is_exe(pid, "NASCAR_GVR.exe")) return TRUE;

    LONG st = GetWindowLong(h, GWL_STYLE);
    const LONG frame = WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                       WS_SYSMENU | WS_BORDER | WS_DLGFRAME;
    if (!(st & frame)) return TRUE;                      // already borderless - leave it be

    SetWindowLong(h, GWL_STYLE, (st & ~frame) | WS_POPUP);
    LONG ex = GetWindowLong(h, GWL_EXSTYLE);
    SetWindowLong(h, GWL_EXSTYLE, ex & ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE |
                                          WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoA(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi);
    int mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
    int w = s->width  < mw ? s->width  : mw;
    int hh = s->height < mh ? s->height : mh;
    int x = mi.rcMonitor.left + (mw - w) / 2, y = mi.rcMonitor.top + (mh - hh) / 2;
    SetWindowPos(h, HWND_TOP, x, y, w, hh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    SetForegroundWindow(h);
    logf("race window %p made borderless: %dx%d at %d,%d", (void*)h, w, hh, x, y);
    return TRUE;
}

// --------------------------------------------------- keep the race above the shell
//
// In borderless mode both the race and the shell are ordinary (non-topmost) windows. The shell
// keeps drawing its "Please Wait" overlay, and depending on timing and the GPU driver it can end
// up on top of the race - which is running and playable, just invisible underneath. borderless_cb
// raises the race exactly once (it returns early once the frame is gone), so nothing kept it in
// front. Here we re-assert the order every poll tick: find the race window, then drop every
// visible shell window to just behind it. SWP_NOACTIVATE means focus never changes, so this does
// not fight alt-tab or steal input - it only reorders z. Skipped in exclusive fullscreen, where
// Direct3D owns the front (this runs only when s.borderless, the windowed path).
static HWND g_raceHwnd;
static BOOL CALLBACK find_race_cb(HWND h, LPARAM)
{
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT rc; GetClientRect(h, &rc);
    if (rc.right < 320 || rc.bottom < 240) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid_is_exe(pid, "NASCAR_GVR.exe")) { g_raceHwnd = h; return FALSE; }   // stop at the first
    return TRUE;
}
static BOOL CALLBACK sink_shell_cb(HWND h, LPARAM lp)
{
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (!pid_is_exe(pid, "AMPlayer.exe")) return TRUE;
    SetWindowPos(h, (HWND)lp, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);  // shell just behind the race
    return TRUE;
}
static void keep_race_above_shell(void)
{
    g_raceHwnd = NULL;
    EnumWindows(find_race_cb, 0);
    if (g_raceHwnd) EnumWindows(sink_shell_cb, (LPARAM)g_raceHwnd);   // no race window yet -> leave the shell alone
}

// ---------------------------------------------------------------- always-on-top
//
// The cabinet shell makes its window always-on-top (WS_EX_TOPMOST), which on a desktop
// swallows alt-tab: nothing can come in front of it. We demote it from here, from outside,
// while we wait (clearing WS_EX_TOPMOST externally is the safe route - hooking SetWindowPos
// in-process can crash a cabinet shell). The race window is demoted too, except in
// exclusive fullscreen, where Direct3D owns that flag and removing it can lose the device.
struct TopmostCtx { bool includeRace; };
static BOOL CALLBACK untopmost_cb(HWND h, LPARAM lp)
{
    if (!IsWindowVisible(h)) return TRUE;
    if (!(GetWindowLong(h, GWL_EXSTYLE) & WS_EX_TOPMOST)) return TRUE;   // cheap test first
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    const TopmostCtx* c = (const TopmostCtx*)lp;
    bool shell = pid_is_exe(pid, "AMPlayer.exe");
    if (!shell && !(c->includeRace && pid_is_exe(pid, "NASCAR_GVR.exe"))) return TRUE;
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    static int n = 0;
    if (n++ < 5 || n % 100 == 0)
        logf("%s window %p was always-on-top -> made a normal window (%d)", shell ? "shell" : "race", (void*)h, n);
    return TRUE;
}

// The taskbar rule: the shell hides the Windows taskbar (kiosk behaviour). We give it back
// whenever the game is NOT the foreground window - alt-tab to anything else and the taskbar
// is there. While the shell or the race is in front we leave it alone: the game covers its
// monitor, so Windows keeps the taskbar behind it anyway.
static bool foreground_is_game(void)
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    return pid_is_exe(pid, "AMPlayer.exe") || pid_is_exe(pid, "NASCAR_GVR.exe");
}

// Starts the child suspended and queues LoadLibraryA(preload) as an APC on its main thread:
// it runs while the loader initialises the process, before any of the program's own code -
// which is what the private registry needs, because AMPlayer reads its keys (GameRoot, to
// find the plug-in list) before it loads the plug-in that would otherwise bring the shim in.
static void preload_dll(const PROCESS_INFORMATION& pi, const char* dll)
{
    SIZE_T n = strlen(dll) + 1;
    void* mem = VirtualAllocEx(pi.hProcess, NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem || !WriteProcessMemory(pi.hProcess, mem, dll, n, NULL) ||
        !QueueUserAPC((PAPCFUNC)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"),
                      pi.hThread, (ULONG_PTR)mem))
        logf("could not preload %s (%lu) - the game's registry falls back to the real one", dll, GetLastError());
    else logf("preloading %s", dll);
}

static DWORD run(const char* exe, const char* args, const char* cwd, bool wait, const Settings& s,
                 const char* preload = NULL)
{
    std::string cmdline = std::string("\"") + exe + "\" " + args;
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    snapshot_taskbar();
    logf("launching: %s", cmdline.c_str());
    logf("  cwd: %s", cwd);
    if (!CreateProcessA(NULL, (LPSTR)cmdline.c_str(), NULL, NULL, FALSE,
                        preload ? CREATE_SUSPENDED : 0, NULL, cwd, &si, &pi)) {
        die("Could not start:\n%s\n\nWindows error %lu.", exe, GetLastError());
    }
    if (preload) { preload_dll(pi, preload); ResumeThread(pi.hThread); }
    give_focus(pi.dwProcessId);
    DWORD code = 0;
    if (wait) {
        // poll while the shell (or a direct race) runs: undo always-on-top, make the race window
        // borderless when asked to, and keep it above the shell's "Please Wait" overlay
        TopmostCtx tc = { !s.fullscreen };
        while (WaitForSingleObject(pi.hProcess, 250) == WAIT_TIMEOUT) {
            EnumWindows(untopmost_cb, (LPARAM)&tc);
            if (s.borderless) { EnumWindows(borderless_cb, (LPARAM)&s); keep_race_above_shell(); }
            if (!foreground_is_game()) restore_taskbar();
        }
        GetExitCodeProcess(pi.hProcess, &code);
        logf("exited with code %lu", code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (wait) restore_taskbar();
    return code;
}

// ---------------------------------------------------------------- main

// ---------------------------------------------------------------- install guard
//
// Antivirus (usually Windows Defender) can quarantine our DLLs after the install - they patch
// code in memory and get preloaded into the front end. The game then hangs or exits with no
// explanation, so check the files that matter before starting anything and name what is gone.
static void check_install(const Settings& s)
{
    struct Need { const char* rel; const char* what; };
    static const Need race[] = {
        { "Game\\NASCAR_GVR.exe", "the race" },
        { "Game\\GvrIO.dll",      "our GvrIO shim for the race (without it the race hangs at start)" },
        { "Game\\GvrIO_oem.dll",  "the original GvrIO the shim forwards to" },
    };
    static const Need shell[] = {
        { "Shell\\bin\\AMPlayer.exe",  "the arcade front end" },
        { "Shell\\Shell.am",           "the front end's start scene" },
        { "Shell\\bin\\GvrIO.dll",     "our GvrIO shim for the front end (without it the front end hangs)" },
        { "Shell\\bin\\GvrIO_oem.dll", "the original GvrIO the shim forwards to" },
        { "Shell\\bin\\Dongle.dll",    "the patched dongle check" },
    };
    static const Need sqlite[] = {          // the SQLite database route (default install)
        { "Shell\\bin\\PLUSDE.dll",    "our database provider (patched to SQLite)" },
        { "Shell\\bin\\GvrSqlite.dll", "our SQLite bridge" },
        { "Shell\\bin\\sqlite3.dll",   "the SQLite engine" },
    };
    std::string missing;
    int n = 0;
    auto check = [&](const Need* list, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            char p[MAX_PATH];
            snprintf(p, sizeof(p), "%s\\%s", g_root, list[i].rel);
            if (PathFileExistsA(p)) continue;
            logf("install check: MISSING %s (%s)", list[i].rel, list[i].what);
            missing += std::string("    ") + list[i].rel + "  - " + list[i].what + "\n";
            ++n;
        }
    };
    check(race, sizeof(race) / sizeof(race[0]));
    if (s.mode != "race") {
        check(shell, sizeof(shell) / sizeof(shell[0]));
        char db[MAX_PATH];
        snprintf(db, sizeof(db), "%s\\GVR\\GvrPlus\\game.db", g_root);
        if (PathFileExistsA(db)) check(sqlite, sizeof(sqlite) / sizeof(sqlite[0]));
    }
    if (!n) {
        logf("install check: all files present");
        // The front end needs .NET 1.1; without it AMPlayer starts and then hangs or fails with no
        // explanation. Installed per machine by Install.bat - copying the folder does not bring it.
        if (s.mode != "race" && !dotnet11_installed())
            die("NASCAR Team Racing is not fully set up on this PC.\n\n"
                "Microsoft .NET Framework 1.1, which the game's menus need, was not found\n"
                "(C:\\Windows\\Microsoft.NET\\Framework\\v1.1.4322).\n\n"
                "It is installed for you by Install.bat - copying the game folder by hand\n"
                "does not install it. Please run Install.bat once, then start the game again.");
        return;
    }
    die("NASCAR Team Racing cannot start: %d file(s) are missing from\n%s\n\n%s\n"
        "The installer put them there, so your antivirus most likely removed them. It is a false "
        "alarm: these small files let an arcade game run on a normal PC.\n\n"
        "Restore them from Windows Security > Virus & threat protection > Protection history, "
        "add this folder as an exclusion, and run Install.bat again to repair the install "
        "(your settings are kept).", n, g_root, missing.c_str());
}

// ---------------------------------------------------------------- display scaling (DPI)
//
// With Windows display scaling above 100% (125% is the laptop default), a program that does not
// declare itself DPI-aware is shown a SMALLER screen (1920x1080 at 125% -> 1536x864) and its
// window is bitmap-stretched by Windows - soft, "stretched" graphics. The launcher, the shell and
// the race are all such programs. So: the launcher declares itself DPI-aware (it then reads the
// real screen size for Width/Height = 0 and places the borderless window in real pixels), and the
// shell and the race inherit the HighDpiAware compatibility layer through __COMPAT_LAYER - no exe
// is changed. [Display] DpiAware=false turns it off.
static void setup_dpi(void)
{
    if (!ini_bool("Display", "DpiAware", true)) { logf("display scaling: DpiAware=false - Windows stretches the game"); return; }
    typedef BOOL (WINAPI *PFN_SetCtx)(HANDLE);
    PFN_SetCtx setCtx = (PFN_SetCtx)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
    if (!(setCtx && setCtx((HANDLE)-4 /*PER_MONITOR_AWARE_V2*/))) SetProcessDPIAware();
    char cur[512] = "";
    GetEnvironmentVariableA("__COMPAT_LAYER", cur, sizeof(cur));
    if (!strstr(cur, "HighDpiAware")) {
        char layer[600];
        snprintf(layer, sizeof(layer), "%s%sHighDpiAware", cur, cur[0] ? " " : "");
        SetEnvironmentVariableA("__COMPAT_LAYER", layer);
    }
    HDC dc = GetDC(NULL);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL, dc);
    logf("display scaling: %d%% - launcher DPI-aware, shell and race run HighDpiAware (no Windows stretching)",
         dpi * 100 / 96);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR cmdline, int)
{
    GetModuleFileNameA(NULL, g_root, MAX_PATH);
    PathRemoveFileSpecA(g_root);
    snprintf(g_ini, sizeof(g_ini), "%s\\nascar_settings.ini", g_root);
    // All diagnostics go into one LOG folder in the install root - the same folder the
    // GvrIO shim and the SQLite provider write to - so a bug report is "zip the LOG folder".
    char logdir[MAX_PATH];
    snprintf(logdir, sizeof(logdir), "%s\\LOG", g_root);
    CreateDirectoryA(logdir, NULL);
    snprintf(g_log, sizeof(g_log), "%s\\nascarlaunch.log", logdir);

    // keep the log from growing forever
    HANDLE hf = CreateFileA(g_log, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz; GetFileSizeEx(hf, &sz); CloseHandle(hf);
        if (sz.QuadPart > 256 * 1024) DeleteFileA(g_log);
    }

    logf("--- NascarLaunch (root: %s) ---", g_root);
    if (!PathFileExistsA(g_ini))
        die("nascar_settings.ini was not found next to the launcher:\n%s", g_ini);
    setup_dpi();

    // [Debug] Log=true also dumps the PC's hardware/driver profile (for "works on my machine"
    // reports) and is the same flag that switches on the GvrIO shim and SQLite provider logs.
    if (ini_bool("Debug", "Log", false)) log_pc_config();

    Settings s;
    load_settings(s);
    check_install(s);

    apply_settings(s);
    ensure_anark_prefs();

    ensure_db_env();

    char gameDir[MAX_PATH], shellDir[MAX_PATH], amPlayer[MAX_PATH], shellAm[MAX_PATH], gameExe[MAX_PATH];
    snprintf(gameDir,  sizeof(gameDir),  "%s\\Game", g_root);
    snprintf(shellDir, sizeof(shellDir), "%s\\Shell", g_root);
    snprintf(amPlayer, sizeof(amPlayer), "%s\\bin\\AMPlayer.exe", shellDir);
    snprintf(shellAm,  sizeof(shellAm),  "%s\\Shell.am", shellDir);
    snprintf(gameExe,  sizeof(gameExe),  "%s\\NASCAR_GVR.exe", gameDir);

    if (s.mode == "race") {
        // Straight into a session, bypassing the cabinet front end. Handy for
        // testing; the shell is the real experience.
        std::string seriesArg;
        if (series_installed(s.series, gameDir)) seriesArg = "-series " + s.series + " ";
        char args[1024];
        snprintf(args, sizeof(args), "-mode single -track %s %s-car %s -laps %d %s -width %d -height %d %s",
                 s.track.c_str(), seriesArg.c_str(), s.car.c_str(), s.laps,
                 s.fullscreen ? "-fullscreen" : "-windowed", s.width, s.height, s.extraArgs.c_str());
        if (!PathFileExistsA(gameExe)) die("NASCAR_GVR.exe was not found:\n%s", gameExe);
        run(gameExe, args, gameDir, s.waitForExit || s.borderless, s);
        return 0;
    }

    // Default: the cabinet. AMPlayer runs Shell.am - attract, then track/car
    // selection, and the shell launches the race itself using the registry
    // values above.
    if (!PathFileExistsA(amPlayer))
        die("The cabinet shell is not installed:\n%s\n\nRe-run the installer without -SkipShell, "
            "or set Mode=race in nascar_settings.ini.", amPlayer);
    if (!PathFileExistsA(shellAm))
        die("Shell.am was not found:\n%s", shellAm);

    char args[MAX_PATH + 32];
    snprintf(args, sizeof(args), "\"%s\" -v FULL", shellAm);
    char binDir[MAX_PATH];
    snprintf(binDir, sizeof(binDir), "%s\\bin", shellDir);
    char shim[MAX_PATH];                       // the GvrIO shim: private registry, pad, fixes
    snprintf(shim, sizeof(shim), "%s\\GvrIO.dll", binDir);
    run(amPlayer, args, binDir, s.waitForExit || s.borderless, s, PathFileExistsA(shim) ? shim : NULL);
    return 0;
}
