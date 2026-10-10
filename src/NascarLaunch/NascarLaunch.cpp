// ============================================================================
//  NascarLaunch.exe - the thing the player actually starts.
//
//  NASCAR Team Racing (GlobalVR), portable install.
//
//  This is the NASCAR counterpart of the NFSU project's GvrLaunch.exe, but it
//  has much less to do, because this game is far more configurable than NFSU:
//
//    * NFSU hardcodes its resolution in the executables, so GvrLaunch has to
//      patch DWORDs in memory at startup. NASCAR reads FullScreenWidth /
//      FullScreenHeight from the registry and also accepts -width/-height/
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
    char msg[1024];
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
    int  car, laps;
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
    s.series        = ini_str("Race", "Series", "2006NEXTEL");
    s.car           = ini_int("Race", "Car", 10);
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

// ---------------------------------------------------------------- the game's registry
//
// The game no longer uses the Windows registry for its own keys: the GvrIO shim answers
// HKLM\SOFTWARE\GlobalVR and HKLM\SOFTWARE\gvr from <install>\nascar_registry.ini
// (GvrIOShim\GvrIOReg.cpp; folder values are computed from the install folder there).
// So the launcher just writes the ini-driven values into that file - no administrator
// rights, nothing machine-wide, and the NFSU install can no longer collide with it.
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


// The cabinet shell HIDES THE WINDOWS TASKBAR - the same lockdown behaviour as the
// OEM's HideTaskBar.exe. If it exits without restoring it, or is killed, the desktop
// is left with no taskbar at all. Put it back whenever we regain control.
static void restore_taskbar(void)
{
    static const char* classes[2] = { "Shell_TrayWnd", "Shell_SecondaryTrayWnd" };
    static int restored = 0;
    for (int i = 0; i < 2; i++) {
        HWND h = NULL;
        while ((h = FindWindowExA(NULL, h, classes[i], NULL)) != NULL) {
            if (!IsWindowVisible(h)) {
                ShowWindow(h, SW_SHOW);
                if (restored++ < 5 || restored % 100 == 0)
                    logf("restored the %s (the shell had hidden it) (%d)", classes[i], restored);
            }
        }
    }
}

// ---------------------------------------------------------------- borderless race
//
// The race is started by the SHELL (AMPlayer.exe), not by us, so we find its window by
// process name. With [Display] Borderless=true the race runs -windowed at Width x Height;
// as soon as its window appears we strip the caption and frame and place it over the
// monitor it is on (centred if it is smaller). The client area then equals the game's
// render size exactly, so the picture is not rescaled. Same idea as the NFSU launcher's
// window handling; only NASCAR_GVR.exe is touched, never the shell.

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
// swallows alt-tab: nothing can come in front of it. Like the NFSU project (which clears
// WS_EX_TOPMOST from outside every second - hooking SetWindowPos in-process crashed that
// shell), we demote it from here while we wait. The race window is demoted too, except in
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

    // [Debug] Log=true also dumps the PC's hardware/driver profile (for "works on my machine"
    // reports) and is the same flag that switches on the GvrIO shim and SQLite provider logs.
    if (ini_bool("Debug", "Log", false)) log_pc_config();

    Settings s;
    load_settings(s);

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
        char args[1024];
        snprintf(args, sizeof(args), "-mode single -track %s -series %s -car %d -laps %d %s -width %d -height %d %s",
                 s.track.c_str(), s.series.c_str(), s.car, s.laps,
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
