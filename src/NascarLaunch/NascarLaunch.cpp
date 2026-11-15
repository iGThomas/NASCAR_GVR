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
#include <ddraw.h>
#include <d3d.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <dxgi1_6.h>
#include "NascarNet.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dbghelp.lib")

static char g_root[MAX_PATH];      // install root (folder holding this exe)
static char g_ini[MAX_PATH];       // nascar_settings.ini
static char g_log[MAX_PATH];       // nascarlaunch.log
static char g_title[64] = "NASCAR GVR";   // message box title: the game's name once known

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
    MessageBoxA(NULL, msg, g_title, MB_ICONERROR | MB_OK);
    ExitProcess(1);
}

// The 2005-2008 programs only use "ANSI" file names: a folder name with letters outside this Windows'
// language (Cyrillic, Greek, Asian ... on a Western Windows) reaches them as '?' and nothing starts
// ("nascar_settings.ini was not found"). Checked with the wide API before anything else runs.
static void check_folder_name(void)
{
    wchar_t w[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, w, MAX_PATH * 2);
    if (!n || n >= MAX_PATH * 2) return;
    BOOL lossy = FALSE;
    char a[MAX_PATH * 4];
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, a, sizeof(a), NULL, &lossy);
    if (!lossy && n < MAX_PATH - 40) return;
    wchar_t* slash = wcsrchr(w, L'\\'); if (slash) *slash = 0;
    wchar_t msg[MAX_PATH * 2 + 600];
    _snwprintf(msg, sizeof(msg) / sizeof(msg[0]),
               L"The game cannot start from this folder:\n\n%s\n\n%s\n\nMove the whole game folder to a simple place, "
               L"for example C:\\Games, and start it from there (or run Setup again and choose that folder).",
               w, lossy ? L"Its name has letters this old game cannot read (only plain Latin letters, numbers, spaces "
                          L"and _ - . work)." : L"Its path is too long for this old game.");
    msg[sizeof(msg) / sizeof(msg[0]) - 1] = 0;
    MessageBoxW(NULL, msg, L"NASCAR GVR", MB_ICONERROR | MB_OK);
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

// A private .NET 1.1 in the install folder (<root>\Runtime\v1.1.4322), used instead of one installed
// in Windows: the .NET loader takes COMPLUS_InstallRoot / COMPLUS_Version from the environment the
// front end inherits (proven on a Windows 10 without .NET 1.1, 2026-11-11). The framework libraries
// it binds to (System, System.Data, ...) must sit next to AMPlayer.exe - there is no library store.
static bool private_runtime(char* dir, size_t n)
{
    char p[MAX_PATH];
    snprintf(dir, n, "%s\\Runtime", g_root);
    snprintf(p, sizeof(p), "%s\\v1.1.4322\\mscorwks.dll", dir);
    return PathFileExistsA(p) != FALSE;
}

static void use_private_runtime(void)
{
    char dir[MAX_PATH];
    if (!private_runtime(dir, sizeof(dir))) return;
    SetEnvironmentVariableA("COMPLUS_InstallRoot", dir);
    SetEnvironmentVariableA("COMPLUS_Version", "v1.1.4322");
    logf(".net 1.1: the front end uses the private copy in %s", dir);
}

// [Debug] Log: the PC's DirectX 7 picture exactly as the Anark front ends' D3DX7 device list builds
// it (DirectDrawEnumerateExA with flags 7 = attached, detached and non-display devices; per device
// DirectDrawCreateEx, SetCooperativeLevel, GetDeviceIdentifier, Direct3D7 EnumDevices) with the three
// capabilities NASCAR Racing 1.1 requires before it offers Direct3D or OpenGL (texture ops 0x5C,
// source blends 0x332, destination blends 0x36 - AMPlayer sub_63B402DF). Logged for every PC, so a
// report from a PC where the game greys out DirectX shows which step or capability is missing.
struct DdEnumDev { GUID guid; bool primary; char desc[128], name[64]; HMONITOR mon; };
struct DdEnumList { DdEnumDev d[12]; int n; };
static BOOL WINAPI dx7_enum_cb(GUID* g, LPSTR desc, LPSTR name, LPVOID p, HMONITOR m)
{
    DdEnumList* l = (DdEnumList*)p;
    if (l->n >= 12) return FALSE;
    DdEnumDev& e = l->d[l->n++];
    e.primary = g == NULL; if (g) e.guid = *g;
    lstrcpynA(e.desc, desc ? desc : "?", sizeof(e.desc)); lstrcpynA(e.name, name ? name : "?", sizeof(e.name));
    e.mon = m;
    return TRUE;
}
static HRESULT CALLBACK dx7_report_dev_cb(LPSTR desc, LPSTR name, LPD3DDEVICEDESC7 d, LPVOID p)
{
    int* count = (int*)p; ++*count;
    const GUID& g = d->deviceGUID;
    const char* kind = IsEqualGUID(g, IID_IDirect3DTnLHalDevice) ? "T&L HAL (hardware)" :
                       IsEqualGUID(g, IID_IDirect3DHALDevice) ? "HAL (hardware)" :
                       IsEqualGUID(g, IID_IDirect3DRGBDevice) ? "RGB (software)" :
                       IsEqualGUID(g, IID_IDirect3DRefDevice) ? "reference (software)" : "other";
    DWORD op = d->dwTextureOpCaps, sb = d->dpcTriCaps.dwSrcBlendCaps, db = d->dpcTriCaps.dwDestBlendCaps;
    logf("directx7:     3D device '%s' %s: texture ops 0x%lX (needs 0x5C: %s), source blends 0x%lX (needs 0x332: %s), "
         "destination blends 0x%lX (needs 0x36: %s), device caps 0x%lX", name ? name : "?", kind,
         op, (op & 0x5C) == 0x5C ? "ok" : "MISSING", sb, (sb & 0x332) == 0x332 ? "ok" : "MISSING",
         db, (db & 0x36) == 0x36 ? "ok" : "MISSING", d->dwDevCaps);
    return D3DENUMRET_OK;
}
static void log_directx7_report(void)
{
    HMODULE dd = LoadLibraryA("ddraw.dll");
    typedef HRESULT (WINAPI *PFN_Enum)(LPDDENUMCALLBACKEXA, LPVOID, DWORD);
    typedef HRESULT (WINAPI *PFN_Create)(GUID*, LPVOID*, REFIID, IUnknown*);
    PFN_Enum   enumEx = dd ? (PFN_Enum)GetProcAddress(dd, "DirectDrawEnumerateExA") : NULL;
    PFN_Create create = dd ? (PFN_Create)GetProcAddress(dd, "DirectDrawCreateEx") : NULL;
    if (!enumEx || !create) { logf("directx7: ddraw.dll not usable (%s)", dd ? "missing exports" : "not loaded"); return; }
    static DdEnumList l; l.n = 0;
    HRESULT hr = enumEx((LPDDENUMCALLBACKEXA)dx7_enum_cb, &l, 7);
    logf("directx7: %d DirectDraw device(s) listed (enumerate hr 0x%08lX)", l.n, (unsigned long)hr);
    for (int i = 0; i < l.n; ++i) {
        DdEnumDev& e = l.d[i];
        char mon[160] = "no monitor";
        MONITORINFOEXA mi; mi.cbSize = sizeof(mi);
        if (e.mon && GetMonitorInfoA(e.mon, &mi))
            snprintf(mon, sizeof(mon), "%s %ldx%ld at %ld,%ld%s", mi.szDevice, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, mi.rcMonitor.left, mi.rcMonitor.top,
                     (mi.dwFlags & MONITORINFOF_PRIMARY) ? " primary" : "");
        logf("directx7: [%d] '%s' (%s)%s, %s", i + 1, e.desc, e.name, e.primary ? " = the primary display driver" : "", mon);
        LPDIRECTDRAW7 ddr = NULL;
        hr = create(e.primary ? NULL : &e.guid, (LPVOID*)&ddr, IID_IDirectDraw7, NULL);
        if (FAILED(hr) || !ddr) { logf("directx7:     DirectDrawCreateEx FAILED (hr 0x%08lX)", (unsigned long)hr); continue; }
        hr = ddr->SetCooperativeLevel(NULL, DDSCL_NORMAL);
        if (FAILED(hr)) logf("directx7:     SetCooperativeLevel FAILED (hr 0x%08lX)", (unsigned long)hr);
        DDDEVICEIDENTIFIER2 id; ZeroMemory(&id, sizeof(id));
        hr = ddr->GetDeviceIdentifier(&id, 0);
        if (SUCCEEDED(hr))
            logf("directx7:     driver %s '%s', vendor %04lX device %04lX subsys %08lX rev %lu, version %u.%u.%u.%u, WHQL %lu",
                 id.szDriver, id.szDescription, id.dwVendorId, id.dwDeviceId, id.dwSubSysId, id.dwRevision,
                 HIWORD(id.liDriverVersion.HighPart), LOWORD(id.liDriverVersion.HighPart),
                 HIWORD(id.liDriverVersion.LowPart), LOWORD(id.liDriverVersion.LowPart), id.dwWHQLLevel);
        else logf("directx7:     GetDeviceIdentifier FAILED (hr 0x%08lX)", (unsigned long)hr);
        DDCAPS caps; ZeroMemory(&caps, sizeof(caps)); caps.dwSize = sizeof(caps);
        if (SUCCEEDED(ddr->GetCaps(&caps, NULL)))
            logf("directx7:     video memory %lu MB, 3D acceleration flag %s", caps.dwVidMemTotal >> 20, (caps.dwCaps & DDCAPS_3D) ? "yes" : "NO");
        LPDIRECT3D7 d3d = NULL;
        hr = ddr->QueryInterface(IID_IDirect3D7, (LPVOID*)&d3d);
        if (FAILED(hr) || !d3d) logf("directx7:     no Direct3D 7 interface (hr 0x%08lX)", (unsigned long)hr);
        else {
            int count = 0;
            hr = d3d->EnumDevices(dx7_report_dev_cb, &count);
            if (!count) logf("directx7:     Direct3D 7 lists NO 3D devices (hr 0x%08lX)", (unsigned long)hr);
            d3d->Release();
        }
        ddr->Release();
    }
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
    // Which adapter drives which screen: a virtual display (VR streaming, "virtual display"
    // drivers) as the primary or at 0,0 gives the front end software OpenGL / no picture.
    for (DWORD i = 0; i < 16; ++i) {
        DISPLAY_DEVICEA dd; dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesA(NULL, i, &dd, 0)) break;
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
            logf("screen  : %s on '%s' - not in use", dd.DeviceName, dd.DeviceString);
            continue;
        }
        DEVMODEA m; m.dmSize = sizeof(m); m.dmDriverExtra = 0;
        bool have = EnumDisplaySettingsA(dd.DeviceName, ENUM_CURRENT_SETTINGS, &m) != FALSE;
        logf("screen  : %s on '%s' - %lux%lu at %ld,%ld%s", dd.DeviceName, dd.DeviceString,
             have ? m.dmPelsWidth : 0, have ? m.dmPelsHeight : 0,
             have ? m.dmPosition.x : 0, have ? m.dmPosition.y : 0,
             (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? " (primary)" : "");
    }
    logf(".net 1.1 in Windows: %s", dotnet11_installed() ? "yes" : "no (fine: the game uses its own copy in Runtime)");
    // The shell's .NET 1.1 code parses US-format dates; the 2.0.2 shim runs the shell as en-US
    // whatever this says. Logged so a report shows the PC's own format at a glance.
    {
        WCHAR loc[LOCALE_NAME_MAX_LENGTH] = L"", sd[64] = L"", tf[64] = L"";
        GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH);
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTDATE, sd, 64);
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, tf, 64);
        logf("region  : %ls (date %ls, time %ls, LCID 0x%04lX)", loc, sd, tf, (unsigned long)GetUserDefaultLCID());
    }
    log_directx7_report();

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

static bool read_whole(const char* path, std::string& out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[8192]; size_t n; out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

// [Graphics] race options, written into the race's player profile (Game\Save\GvrSinglePlayer\
// GvrSinglePlayer.PLR, both games) at every start - the arcade has no options screen for them, so the
// settings file is where a player sets them. An empty value leaves the profile's own; an out-of-range one is
// logged and skipped. The profile is player data (kept by updates); only these values are touched.
struct PlrOption { const char* key; const char* plr; int lo, hi; };
static const PlrOption kPlrOptions[] = {
    { "AITextureSize",      "AI max texture size",  0, 4096 },
    { "EnvironmentMap",     "Environment Map",      0, 1 },
    { "CompressedTextures", "Compressed Textures",  0, 1 },
    { "DynamicLighting",    "Dynamic Lighting",     0, 3 },
    { "MaxVisibleCars",     "Max Visible Vehicles", 5, 48 },
};

static void apply_race_graphics(void)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\Game\\Save\\GvrSinglePlayer\\GvrSinglePlayer.PLR", g_root);
    std::string text, orig;
    if (!read_whole(path, text)) return;
    orig = text;
    std::string done;
    for (const PlrOption& o : kPlrOptions) {
        std::string v = ini_str("Graphics", o.key, "");
        while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
        while (!v.empty() && isspace((unsigned char)v[0])) v.erase(0, 1);
        if (v.empty()) continue;
        char* end = NULL; long n = strtol(v.c_str(), &end, 10);
        if (*end || n < o.lo || n > o.hi) { logf("race graphics: [Graphics] %s = '%s' is not %d to %d - skipped", o.key, v.c_str(), o.lo, o.hi); continue; }
        // the line that starts with the option's name: <name> <tabs> = "<value>"
        size_t at = std::string::npos, from = 0, kl = strlen(o.plr);
        while ((from = text.find(o.plr, from)) != std::string::npos) {
            if ((from == 0 || text[from - 1] == '\n') && (text[from + kl] == ' ' || text[from + kl] == '\t' || text[from + kl] == '=')) { at = from; break; }
            from += kl;
        }
        size_t eol = at == std::string::npos ? at : text.find('\n', at);
        size_t q1 = at == std::string::npos ? at : text.find('"', at);
        size_t q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
        if (q2 == std::string::npos || (eol != std::string::npos && q2 > eol)) { logf("race graphics: '%s' not found in the player profile", o.plr); continue; }
        std::string want = std::to_string(n);
        if (text.compare(q1 + 1, q2 - q1 - 1, want) != 0) {
            done += (done.empty() ? "" : ", ") + std::string(o.key) + " " + text.substr(q1 + 1, q2 - q1 - 1) + " -> " + want;
            text.replace(q1 + 1, q2 - q1 - 1, want);
        }
    }
    if (text == orig) return;
    FILE* f = fopen(path, "wb");
    if (!f) { logf("race graphics: could not write %s (%lu)", path, GetLastError()); return; }
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    logf("race graphics: %s", done.c_str());
}

// [Graphics] MaxCarDetail (NASCAR Racing): the cars' model lists (.gen) give each body part several models by
// distance; "max" draws the most detailed one at every distance ("Force LOD A", a player's suggestion). The
// installer keeps the disc's lists as <name>.gen.oem and makes the max-detail ones as <name>.gen.loda; the
// chosen one is put in place at every start. Nothing to do where the installer did not prepare them.
static void apply_car_detail(void)
{
    char dir[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\Game\\GameData\\Vehicles\\2005 NEXTEL Cup", g_root);
    const bool maxd = ini_bool("Graphics", "MaxCarDetail", false);
    const char* gens[] = { "2005_NEXTEL_Cup.gen", "2005_NEXTEL_Cup_Fantasy.gen" };
    int prepared = 0, copied = 0;
    for (int i = 0; i < 2; i++) {
        char dst[MAX_PATH], src[MAX_PATH];
        snprintf(dst, sizeof dst, "%s\\%s", dir, gens[i]);
        snprintf(src, sizeof src, "%s\\%s.%s", dir, gens[i], maxd ? "loda" : "oem");
        std::string a, b;
        if (!read_whole(src, a)) continue;
        prepared++;
        if (read_whole(dst, b) && a == b) continue;
        if (CopyFileA(src, dst, FALSE)) copied++;
        else logf("car detail: could not write %s (%lu)", dst, GetLastError());
    }
    if (prepared)
        logf("car detail: %s%s", maxd ? "max - the most detailed models at every distance" : "as on the cabinet",
             copied ? "" : " (already in place)");
}

// ---------------------------------------------------------------- settings

struct Settings {
    int  width, height;
    bool fullscreen;
    bool borderless;         // windowed race, frame stripped, covering its monitor
    int  musicVolume;
    int  simpleAttract;
    bool extraDrivers;       // 1.1: the 19 extra disc cars on the driver select screen
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
    s.extraDrivers  = ini_bool("Cabinet", "ExtraDrivers", true);
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
    store_set("ExtraDrivers", s.extraDrivers ? "1" : "0");
    // [Multiplayer] PlayerName: shown to the other players (Lobby cards; the GvrIO shim puts it on the car
    // label in a linked race). Up to 15 characters (the race's label size); no | ; " (they separate fields
    // in the menus' messages and the ini). Empty = the game's own "Driver 1", "Driver 2".
    {
        std::string raw = ini_str("Multiplayer", "PlayerName", ""), name;
        for (size_t i = 0; i < raw.size() && name.size() < 15; i++) {
            unsigned char ch = (unsigned char)raw[i];
            if (ch >= 32 && ch < 127 && ch != '|' && ch != ';' && ch != '"') name += (char)ch;
        }
        while (!name.empty() && name[0] == ' ') name.erase(0, 1);
        store_set("PlayerName", name);
    }
    store_set("cmd", compose_cmd(s));
    logf("settings -> nascar_registry.ini: %dx%d %s, cmd=\"%s\"", s.width, s.height,
         s.borderless ? "borderless" : (s.fullscreen ? "fullscreen" : "windowed"), compose_cmd(s).c_str());
}

// NASCAR Racing 1.1: the extra drivers' pictures (faces, name plates, numbers) are built on this PC
// from the disc's own files by Tools\GvrDriverArt.exe; players may replace the pictures in
// Shell\Textures\Faces, Names and Numbers. "sheets" rebuilds what the scene reads only when one of
// those files changed since the last start (it compares a signature file), so a normal start costs
// well under a second. A failure is logged and the shell still starts (the extra drivers then show
// the pictures of the previous build, or blank cards).
static bool is_teamracing_shell(const char* amPlayer);
static void build_driver_art(const Settings& s, const char* amPlayer)
{
    if (!s.extraDrivers) return;
    char exe[MAX_PATH];
    snprintf(exe, sizeof(exe), "%s\\Tools\\GvrDriverArt.exe", g_root);
    if (!PathFileExistsA(exe) || is_teamracing_shell(amPlayer)) return;
    char cmd[MAX_PATH * 2 + 32];
    snprintf(cmd, sizeof(cmd), "\"%s\" sheets \"%s\"", exe, g_root);
    STARTUPINFOA si = { sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };
    DWORD t0 = GetTickCount();
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_root, &si, &pi)) {
        logf("driver art: could not start %s (error %lu)", exe, GetLastError());
        return;
    }
    DWORD code = 0;
    if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 3); code = 3;
        logf("driver art: no answer after 60 s - stopped, the shell starts with the previous pictures");
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
        logf("driver art: exit %lu after %lu ms%s", code, GetTickCount() - t0,
             code == 0 ? "" : " (see LOG\\gvrdriverart.log)");
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
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

// ---------------------------------------------------------------- arcade lobby (menus' cabinet link)
//
// [Multiplayer] Mode=lobby links the menus through the relay: the link plug-in goes back into the
// shell's plug-in list (the installer takes it out, because the original GvrLinking.dll hangs the
// menus without IPX). Only with the relay GvrLinking.dll in place, and only for NASCAR Racing 1.1;
// any other mode takes the plug-in out again, so single-player menus stay exactly as before.
static void apply_link_plugin(bool lobby, const char* amPlayer)
{
    char cfg[MAX_PATH], dll[MAX_PATH], plug[MAX_PATH];
    snprintf(cfg, sizeof cfg, "%s\\Game\\config\\GvrShellPlugInList.xml", g_root);
    snprintf(dll, sizeof dll, "%s\\Shell\\bin\\GvrLinking.dll", g_root);
    snprintf(plug, sizeof plug, "%s\\Shell\\bin\\plugins\\GvrLinkingPlugIn.dll", g_root);
    std::string xml, bin;
    if (!read_whole(cfg, xml)) return;
    const char* tag = "<GvrLinkingPlugIn />";
    size_t at = xml.find(tag);
    bool ours = read_whole(dll, bin) && bin.find("relay link for the menus") != std::string::npos;
    bool want = lobby && ours && PathFileExistsA(plug) && !is_teamracing_shell(amPlayer);
    if (lobby && !want)
        logf("multiplayer lobby: not available (%s)", is_teamracing_shell(amPlayer) ? "NASCAR Racing 1.1 only for now"
             : !ours ? "Shell\\bin\\GvrLinking.dll is not the relay link" : "GvrLinkingPlugIn.dll missing");
    if (want == (at != std::string::npos)) {
        if (want) logf("multiplayer lobby: link plug-in on");
        return;
    }
    if (want) {
        size_t end = xml.find("</GvrShell>");
        if (end == std::string::npos) return;
        xml.insert(end, std::string("\t") + tag + "\r\n");
    } else {
        size_t ls = xml.rfind('\n', at);
        size_t le = xml.find('\n', at);
        xml.erase(ls == std::string::npos ? at : ls + 1, (le == std::string::npos ? xml.size() : le + 1) - (ls == std::string::npos ? at : ls + 1));
    }
    FILE* f = fopen(cfg, "wb");
    if (!f) { logf("multiplayer lobby: cannot write %s", cfg); return; }
    fwrite(xml.data(), 1, xml.size(), f);
    fclose(f);
    logf("multiplayer lobby: link plug-in %s", want ? "turned on" : "turned off");
}

// ---------------------------------------------------------------- 2.0.2 renderer
//
// NASCAR Team Racing's front end (AMPlayer.exe, Anark Client 4.0, link stamp 0x47C34847) uses the
// renderer saved in HKCU\Software\Anark\Client\4.0\Preferences ClientRenderer. OpenGL (2) is the
// one that survives alt-tab on Windows 10/11 (its Direct3D one, 3, deadlocks in DirectDraw), but
// it needs a hardware OpenGL driver: on a screen driven by a virtual display adapter Windows hands
// out its software OpenGL 1.1, the front end greys OpenGL out and shows no usable picture. So
// [Display] Renderer = auto probes OpenGL the way the front end does (a hidden window at 0,0,
// pixel format #1's description, a real context) and picks OpenGL with hardware OpenGL 1.3+,
// else Direct3D. opengl / directx force one.
static DWORD shell_stamp(const char* amPlayer)          // AMPlayer.exe link stamp, 0 if unreadable
{
    HANDLE f = CreateFileA(amPlayer, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    BYTE h[1024]; DWORD got = 0;
    BOOL ok = ReadFile(f, h, sizeof(h), &got, NULL);
    CloseHandle(f);
    if (!ok || got < 0x40) return 0;
    DWORD pe = *(DWORD*)(h + 0x3C);
    return pe + 12 <= got ? *(DWORD*)(h + pe + 8) : 0;
}

static bool is_teamracing_shell(const char* amPlayer) { return shell_stamp(amPlayer) == 0x47C34847; }

// NASCAR Racing 1.1's front end (Anark Client 3.0, stamp 0x46AFA277) offers Direct3D 7 only when a
// D3D7 device of the screen's driver has texture ops ADD|MODULATE2X|MODULATE|SELECTARG2 (0x5C),
// source blends 0x332 and destination blends 0x36 (AMPlayer sub_63B402DF over D3DDEVICEDESC7); when
// no device passes it greys out Direct3D AND OpenGL and draws in software - at 2560x1600 a frozen
// "Please Wait" with the music playing. This repeats that test on the primary display driver.
static bool probe_opengl(char* what, size_t n);
struct D3d7Probe { bool ok; char devs[300]; };
static HRESULT CALLBACK d3d7_dev_cb(LPSTR desc, LPSTR name, LPD3DDEVICEDESC7 d, LPVOID p)
{
    D3d7Probe* r = (D3d7Probe*)p;
    bool hw = IsEqualGUID(d->deviceGUID, IID_IDirect3DHALDevice) || IsEqualGUID(d->deviceGUID, IID_IDirect3DTnLHalDevice);
    bool caps = (d->dwTextureOpCaps & 0x5C) == 0x5C && (d->dpcTriCaps.dwSrcBlendCaps & 0x332) == 0x332 &&
                (d->dpcTriCaps.dwDestBlendCaps & 0x36) == 0x36;
    if (hw && caps) r->ok = true;
    size_t n = strlen(r->devs);
    if (n < sizeof(r->devs) - 64)
        snprintf(r->devs + n, sizeof(r->devs) - n, "%s%s (%s%s)", n ? ", " : "", name ? name : "?",
                 hw ? "hardware" : "software", hw ? (caps ? ", passes" : ", lacks blend caps") : "");
    return D3DENUMRET_OK;
}

static bool probe_d3d7(char* what, size_t n)
{
    snprintf(what, n, "no DirectX 7");
    HMODULE dd = LoadLibraryA("ddraw.dll");
    typedef HRESULT (WINAPI *PFN_Create)(GUID*, LPVOID*, REFIID, IUnknown*);
    PFN_Create create = dd ? (PFN_Create)GetProcAddress(dd, "DirectDrawCreateEx") : NULL;
    LPDIRECTDRAW7 ddraw = NULL; LPDIRECT3D7 d3d = NULL;
    D3d7Probe r = { false, "" };
    if (create && SUCCEEDED(create(NULL, (LPVOID*)&ddraw, IID_IDirectDraw7, NULL)) && ddraw) {
        ddraw->SetCooperativeLevel(NULL, DDSCL_NORMAL);
        if (SUCCEEDED(ddraw->QueryInterface(IID_IDirect3D7, (LPVOID*)&d3d)) && d3d) {
            d3d->EnumDevices(d3d7_dev_cb, &r);
            snprintf(what, n, "DirectX 7 devices: %s", r.devs[0] ? r.devs : "none");
            d3d->Release();
        } else snprintf(what, n, "no Direct3D 7 on the primary display driver");
        ddraw->Release();
    }
    return r.ok;
}

// NASCAR Racing 1.1: [Display] Renderer = auto keeps Direct3D 7 wherever the game's own test passes
// (unchanged); where it fails, OpenGL with hardware OpenGL 1.3+ (a saved ClientRenderer is used
// without the game's test). opengl / directx force one. HKCU\Software\Anark\Client\3.0\Preferences.
// ---------------------------------------------------------------- graphics picture
//
// Which graphics chips this PC has, which one drives the main screen and which one Windows calls
// "high performance". Used to pick the right chip on two-chip PCs ([Display] Gpu) and to explain a PC
// where neither of the menus' renderers works.
struct GfxInfo {
    int hw = 0;                    // hardware adapters (Windows' software renderer not counted)
    bool basic = false;            // no graphics driver: Windows' basic display driver
    bool laptop = false;           // has a battery
    std::string primary;           // adapter driving the main screen
    std::string highPerf;          // adapter Windows picks for "high performance"
    std::string list;
};
static GfxInfo g_gfx;
static bool g_gfxDone;

static std::string lower_str(std::string s)
{
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]);
    return s;
}

static const GfxInfo& graphics_info(void)
{
    if (g_gfxDone) return g_gfx;
    g_gfxDone = true;
    HMODULE m = LoadLibraryA("dxgi.dll");
    typedef HRESULT (WINAPI *CreateFactory1)(REFIID, void**);
    CreateFactory1 cf = m ? (CreateFactory1)GetProcAddress(m, "CreateDXGIFactory1") : NULL;
    IDXGIFactory1* f = NULL;
    if (cf && SUCCEEDED(cf(__uuidof(IDXGIFactory1), (void**)&f)) && f) {
        IDXGIAdapter1* a = NULL;
        for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d;
            if (SUCCEEDED(a->GetDesc1(&d))) {
                char name[128];
                WideCharToMultiByte(CP_ACP, 0, d.Description, -1, name, sizeof(name), NULL, NULL);
                bool sw = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || d.VendorId == 0x1414;   // Microsoft Basic Render
                if (!sw) g_gfx.hw++;
                g_gfx.list += std::string(g_gfx.list.empty() ? "" : ", ") + name + (sw ? " (software)" : "");
            }
            a->Release();
        }
        IDXGIFactory6* f6 = NULL;                  // Windows 10 1803+: the OS's own "high performance" choice
        if (SUCCEEDED(f->QueryInterface(__uuidof(IDXGIFactory6), (void**)&f6)) && f6) {
            IDXGIAdapter1* hp = NULL;
            if (SUCCEEDED(f6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1), (void**)&hp)) && hp) {
                DXGI_ADAPTER_DESC1 d;
                if (SUCCEEDED(hp->GetDesc1(&d))) {
                    char name[128];
                    WideCharToMultiByte(CP_ACP, 0, d.Description, -1, name, sizeof(name), NULL, NULL);
                    g_gfx.highPerf = name;
                }
                hp->Release();
            }
            f6->Release();
        }
        f->Release();
    }
    for (DWORD i = 0; i < 16; ++i) {
        DISPLAY_DEVICEA dd; dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesA(NULL, i, &dd, 0)) break;
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) { g_gfx.primary = dd.DeviceString; break; }
    }
    std::string p = lower_str(g_gfx.primary);
    g_gfx.basic = g_gfx.hw == 0 || p.find("basic display") != std::string::npos || p.find("basic render") != std::string::npos;
    SYSTEM_POWER_STATUS ps;
    g_gfx.laptop = GetSystemPowerStatus(&ps) && ps.BatteryFlag != 128 && ps.BatteryFlag != 255;
    logf("graphics: %s | main screen on '%s' | high performance = '%s'%s",
         g_gfx.list.empty() ? "(none found)" : g_gfx.list.c_str(), g_gfx.primary.c_str(), g_gfx.highPerf.c_str(),
         g_gfx.laptop ? " | laptop" : "");
    return g_gfx;
}

// [Display] Gpu: on a PC with two graphics chips (most gaming laptops) Windows runs an unknown old program on
// the built-in, power-saving one, whose old Direct3D 7 / OpenGL often fails the menus' test. auto = ask Windows
// for the high-performance chip for the launcher, the menus and the race (the same setting as Windows'
// Settings > Display > Graphics; a choice already made there is kept). high / low = always that chip;
// windows = remove the choice again. Returns true when the launcher's own choice changed (restart it once, so
// its graphics tests run on the chip the menus will use).
static bool apply_gpu_preference(void)
{
    std::string want = lower_str(ini_str("Display", "Gpu", "auto"));
    const char* rel[] = { "NascarLaunch.exe", "Shell\\bin\\AMPlayer.exe", "Game\\NASCAR_GVR.exe" };
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, NULL, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return false;
    bool launcherChanged = false;
    if (want == "auto" && graphics_info().hw < 2) { RegCloseKey(k); return false; }   // one chip: nothing to choose
    const char* value = want == "low" ? "GpuPreference=1;" : "GpuPreference=2;";
    // what the launcher itself wrote, so "windows" undoes only that and never a choice the player made in Windows
    // (section [NascarLaunch] of nascar_registry.ini: no registry key of the game maps to it)
    char store[MAX_PATH];
    snprintf(store, sizeof(store), "%s\\nascar_registry.ini", g_root);
    for (int i = 0; i < 3; i++) {
        char path[MAX_PATH], mark[8], ours[64] = "";
        snprintf(path, sizeof(path), "%s\\%s", g_root, rel[i]);
        snprintf(mark, sizeof(mark), "Gpu%d", i);
        GetPrivateProfileStringA("NascarLaunch", mark, "", ours, sizeof(ours), store);
        char have[64] = ""; DWORD t = 0, n = sizeof(have);
        bool exists = RegQueryValueExA(k, path, NULL, &t, (BYTE*)have, &n) == ERROR_SUCCESS;
        bool mine = exists && ours[0] && strcmp(have, ours) == 0;
        if (want == "windows") {
            if (mine) { RegDeleteValueA(k, path); logf("gpu: Windows decides again for %s", rel[i]); }
            else if (exists) logf("gpu: %s keeps the choice made in Windows (%s)", rel[i], have);
            if (ours[0]) WritePrivateProfileStringA("NascarLaunch", mark, NULL, store);
            continue;
        }
        if (want == "auto" && exists) {
            logf("gpu: %s keeps %s (%s)", rel[i], mine ? "the launcher's earlier choice" : "the choice made in Windows", have);
            continue;
        }
        if (exists && strcmp(have, value) == 0) continue;
        RegSetValueExA(k, path, 0, REG_SZ, (const BYTE*)value, (DWORD)strlen(value) + 1);
        WritePrivateProfileStringA("NascarLaunch", mark, value, store);
        logf("gpu: %s -> %s graphics chip", rel[i], want == "low" ? "power-saving" : "high-performance");
        if (i == 0) launcherChanged = true;
    }
    RegCloseKey(k);
    return launcherChanged;
}

// [Display] DDrawCompat: DDrawCompat (open source, 0BSD, github.com/narzoul/DDrawCompat) ships switched off as
// Shell\bin\ddraw-compat.dll and becomes Shell\bin\ddraw.dll - which the menus then load instead of Windows' own -
// only when needed: auto = when this PC fails both of the menus' graphics tests, on = always, off = never.
static bool same_size(const char* a, const char* b)
{
    WIN32_FILE_ATTRIBUTE_DATA x, y;
    return GetFileAttributesExA(a, GetFileExInfoStandard, &x) && GetFileAttributesExA(b, GetFileExInfoStandard, &y) &&
           x.nFileSizeLow == y.nFileSizeLow && x.nFileSizeHigh == y.nFileSizeHigh;
}

static bool set_ddrawcompat(const char* amPlayer, bool on, const char* why)
{
    char dir[MAX_PATH], src[MAX_PATH], live[MAX_PATH];
    strcpy(dir, amPlayer); PathRemoveFileSpecA(dir);
    snprintf(src, sizeof(src), "%s\\ddraw-compat.dll", dir);
    snprintf(live, sizeof(live), "%s\\ddraw.dll", dir);
    bool ours = PathFileExistsA(live) && PathFileExistsA(src) && same_size(src, live);
    if (on) {
        if (!PathFileExistsA(src)) { logf("ddrawcompat: wanted (%s) but not in this install", why); return false; }
        if (!ours && !CopyFileA(src, live, FALSE)) { logf("ddrawcompat: could not switch it on (%lu)", GetLastError()); return false; }
        logf("ddrawcompat: ON for the menus (%s)", why);
        return true;
    }
    if (ours) { DeleteFileA(live); logf("ddrawcompat: off (%s)", why); }
    return false;
}

static std::string ddrawcompat_mode(void)
{
    std::string m = lower_str(ini_str("Display", "DDrawCompat", "auto"));
    return (m == "on" || m == "off") ? m : "auto";
}

// Neither renderer works: name the likely cause in plain words. DDrawCompat cannot help with a missing driver,
// a remote / virtual screen or a monitor on the wrong chip, so those always get the message.
static void explain_no_renderer(bool compatOn)
{
    const GfxInfo& g = graphics_info();
    std::string p = lower_str(g.primary);
    const char* virt[] = { "remote display", "parsec", "virtual", "idd", "spacedesk", "meta ", "oculus", "duet",
                           "splashtop", "sunshine", "citrix", "vmware", "virtualbox", "hyper-v", "teamviewer", "anydesk" };
    bool isVirt = false;
    for (size_t i = 0; i < sizeof(virt) / sizeof(virt[0]); i++) if (p.find(virt[i]) != std::string::npos) isVirt = true;
    bool wrongChip = g.hw >= 2 && !p.empty() && !g.highPerf.empty() && lower_str(g.highPerf) != p;
    std::string why;
    if (g.basic)
        why = "No graphics driver is installed: Windows uses its basic display driver. Install the driver for your "
              "graphics card from the NVIDIA, AMD or Intel website, then start the game again.";
    else if (isVirt)
        why = "Your main screen is a remote or virtual display ('" + g.primary + "'). Start the game on a monitor "
              "connected to the graphics card (close Remote Desktop, streaming or virtual-screen tools first).";
    else if (wrongChip && !g.laptop)
        why = "Your monitor is connected to the motherboard's built-in graphics ('" + g.primary + "'). "
              "Plug the monitor cable into your graphics card ('" + g.highPerf + "') instead.";
    else if (compatOn) {
        logf("renderer: neither test passed - menus use Direct3D 7 through DDrawCompat");
        return;
    } else
        why = "Your graphics driver does not offer the old Direct3D 7 or OpenGL these 2005 menus need. Update the "
              "graphics driver. If the menus stay on \"Please Wait\", set DDrawCompat = on under [Display] in "
              "nascar_settings.ini.";
    logf("renderer: neither test passed - %s", why.c_str());
    std::string msg = "The menus may stay on \"Please Wait\" on this PC: they cannot use your graphics card.\n\n" + why +
                      "\n\nThe game starts anyway.";
    MessageBoxA(NULL, msg.c_str(), g_title, MB_ICONWARNING | MB_OK);
}

static void choose_classic_renderer(const char* amPlayer)
{
    if (shell_stamp(amPlayer) != 0x46AFA277) return;
    std::string want = ini_str("Display", "Renderer", "auto");
    for (size_t i = 0; i < want.size(); i++) want[i] = (char)tolower((unsigned char)want[i]);
    char d3d[320] = "", gl[256] = "";
    int r;                                         // 3 Direct3D, 2 OpenGL, 0 = leave it to the game
    bool none = false, compatOn = false;
    std::string compat = ddrawcompat_mode();
    if (want == "directx") r = 3;
    else if (want == "opengl") r = 2;
    else {
        if (want != "auto") logf("renderer: [Display] Renderer = '%s' is not auto, opengl or directx - using auto", want.c_str());
        bool dx = probe_d3d7(d3d, sizeof(d3d));
        r = dx ? 0 : probe_opengl(gl, sizeof(gl)) ? 2 : 0;
        none = !dx && r == 0;
    }
    if (compat == "on") compatOn = set_ddrawcompat(amPlayer, true, "set in [Display] DDrawCompat");
    else if (compat == "auto" && none) compatOn = set_ddrawcompat(amPlayer, true, "this PC fails both graphics tests");
    else set_ddrawcompat(amPlayer, false, compat == "off" ? "set in [Display] DDrawCompat" : "not needed here");
    if (none && compatOn) r = 3;                   // Direct3D 7 through DDrawCompat rather than software
    if (none) explain_no_renderer(compatOn);
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Anark\\Client\\3.0\\Preferences", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        if (r) {
            DWORD v = (DWORD)r;
            RegSetValueExA(k, "ClientRenderer", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
            RegSetValueExA(k, "DisableGL", 0, REG_SZ, (const BYTE*)(r == 2 ? "0" : "1"), 2);
        } else {                                   // the game decides, as on the cabinet
            RegDeleteValueA(k, "ClientRenderer");
            RegSetValueExA(k, "DisableGL", 0, REG_SZ, (const BYTE*)"1", 2);
        }
        RegCloseKey(k);
    }
    if (want == "directx" || want == "opengl")
        logf("renderer: %s (set in [Display] Renderer)", r == 2 ? "OpenGL" : "Direct3D 7");
    else if (r == 2)
        logf("renderer: OpenGL (%s) - this PC fails the game's Direct3D 7 test (%s)", gl, d3d);
    else if (d3d[0] && strstr(d3d, "passes"))
        logf("renderer: Direct3D 7, as on the cabinet (%s)", d3d);
    else if (none && compatOn)
        logf("renderer: Direct3D 7 through DDrawCompat - this PC fails both tests (Direct3D 7: %s; OpenGL: %s)", d3d, gl);
    else
        logf("renderer: WARNING - neither Direct3D 7 (%s) nor hardware OpenGL (%s): the game draws in software (very slow)", d3d, gl);
}

static bool probe_opengl(char* what, size_t n)
{
    snprintf(what, n, "no OpenGL");
    HMODULE gl = LoadLibraryA("opengl32.dll");
    if (!gl) return false;
    typedef HGLRC (WINAPI *PFN_Create)(HDC);
    typedef BOOL (WINAPI *PFN_Make)(HDC, HGLRC);
    typedef BOOL (WINAPI *PFN_Delete)(HGLRC);
    typedef const char* (WINAPI *PFN_Str)(unsigned int);
    PFN_Create create = (PFN_Create)GetProcAddress(gl, "wglCreateContext");
    PFN_Make   make   = (PFN_Make)GetProcAddress(gl, "wglMakeCurrent");
    PFN_Delete del    = (PFN_Delete)GetProcAddress(gl, "wglDeleteContext");
    PFN_Str    str    = (PFN_Str)GetProcAddress(gl, "glGetString");
    bool hw = false;
    WNDCLASSA wc = {}; wc.style = CS_OWNDC; wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "NascarLaunchGlProbe";
    RegisterClassA(&wc);
    HWND w = CreateWindowExA(0, wc.lpszClassName, "", WS_POPUP, 0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
    HDC dc = w ? GetDC(w) : NULL;
    if (dc && create && make && del && str) {
        PIXELFORMATDESCRIPTOR pfd;
        DescribePixelFormat(dc, 1, sizeof(pfd), &pfd);
        int pf = ChoosePixelFormat(dc, &pfd);
        HGLRC rc = (pf && SetPixelFormat(dc, pf, &pfd)) ? create(dc) : NULL;
        if (rc && make(dc, rc)) {
            const char* ven = str(0x1F00); const char* ren = str(0x1F01); const char* ver = str(0x1F02);
            int major = ver ? atoi(ver) : 0, minor = 0;
            const char* dot = ver ? strchr(ver, '.') : NULL;
            if (dot) minor = atoi(dot + 1);
            snprintf(what, n, "%s / %s / OpenGL %s", ven ? ven : "?", ren ? ren : "?", ver ? ver : "?");
            hw = (major > 1 || (major == 1 && minor >= 3)) && !(ren && strstr(ren, "GDI Generic"));
            make(NULL, NULL);
        } else {
            snprintf(what, n, "no OpenGL context (pixel format %d)", pf);
        }
        if (rc) del(rc);
    }
    if (dc) ReleaseDC(w, dc);
    if (w) DestroyWindow(w);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return hw;
}

static void choose_teamracing_renderer(const char* amPlayer)
{
    if (!is_teamracing_shell(amPlayer)) return;
    // The front end logs to the Windows event source "GVR"; without it .NET 1.1 tries to create
    // the source (administrator only) and the menus go black at start-up. The installer registers it.
    HKEY ev;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\GVR", 0,
                      KEY_READ, &ev) == ERROR_SUCCESS) RegCloseKey(ev);
    else logf("WARNING: Windows event source GVR is missing - the menus will go black at start-up. "
              "Run Install.bat again (it registers it).");
    std::string want = ini_str("Display", "Renderer", "auto");
    for (size_t i = 0; i < want.size(); i++) want[i] = (char)tolower((unsigned char)want[i]);
    DWORD r = 2;
    char what[256] = "";
    if (want == "directx") { r = 3; snprintf(what, sizeof(what), "set in [Display] Renderer"); }
    else if (want == "opengl") { r = 2; snprintf(what, sizeof(what), "set in [Display] Renderer"); }
    else {
        if (want != "auto") logf("renderer: [Display] Renderer = '%s' is not auto, opengl or directx - using auto", want.c_str());
        r = probe_opengl(what, sizeof(what)) ? 2 : 3;
    }
    bool none = false, compatOn = false;
    if (r == 3 && want != "directx") {             // no hardware OpenGL: does Direct3D work at least?
        char d3d[320] = "";
        none = !probe_d3d7(d3d, sizeof(d3d));
        if (none) logf("renderer: Direct3D test fails too (%s)", d3d);
    }
    std::string compat = ddrawcompat_mode();
    if (compat == "on") compatOn = set_ddrawcompat(amPlayer, true, "set in [Display] DDrawCompat");
    else if (compat == "auto" && none) compatOn = set_ddrawcompat(amPlayer, true, "this PC fails both graphics tests");
    else set_ddrawcompat(amPlayer, false, compat == "off" ? "set in [Display] DDrawCompat" : "not needed here");
    if (none) explain_no_renderer(compatOn);
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Anark\\Client\\4.0\\Preferences", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(k, "ClientRenderer", 0, REG_DWORD, (const BYTE*)&r, sizeof(r));
        RegCloseKey(k);
    }
    logf("renderer: %s (%s)%s", r == 2 ? "OpenGL" : "Direct3D", what,
         r == 3 && want == "auto" ? " - no hardware OpenGL here: Direct3D instead (alt-tab may freeze the menus)" : "");
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

// ---------------------------------------------------------------- session diagnostics ([Debug] Log)
//
// 1. Debug output. The front ends and the race report script errors, .NET exceptions and progress
//    ("GvrShell Loading NASCAR_Attract.am", "Activating Slide 0") only through OutputDebugString,
//    which a player cannot capture. While the session runs the launcher is the DBWIN listener and
//    writes every line from a program inside the install folder to LOG\debug-output.log (8 MB cap).
//    If another listener (a debugger, DebugView) already owns DBWIN it steps aside.
// 2. After the session: Windows' own crash records for the game's programs (Event Log) and the race
//    engine's traces (Game\LOG\trace*.txt) of this session, so one zipped LOG folder has them.
static FILE* g_dbgFile = NULL;
static SYSTEMTIME g_sessionStart;
static FILETIME   g_sessionStartFt;

struct DbwinCtx { HANDLE ready, data; const char* view; };
static bool pid_is_ours(DWORD pid)
{
    static DWORD cachePid[64]; static char cacheOk[64]; static int n = 0;
    for (int i = 0; i < n; ++i) if (cachePid[i] == pid) return cacheOk[i] != 0;
    typedef BOOL (WINAPI *PFN_Q)(HANDLE, DWORD, LPSTR, PDWORD);
    static PFN_Q q = (PFN_Q)GetProcAddress(GetModuleHandleA("kernel32.dll"), "QueryFullProcessImageNameA");
    char path[MAX_PATH] = ""; DWORD len = MAX_PATH;
    HANDLE h = OpenProcess(0x1000 /*PROCESS_QUERY_LIMITED_INFORMATION*/, FALSE, pid);
    bool ok = false;
    if (h) { ok = q && q(h, 0, path, &len) && _strnicmp(path, g_root, strlen(g_root)) == 0; CloseHandle(h); }
    if (pid == GetCurrentProcessId()) ok = false;
    int slot = n < 64 ? n++ : (int)(pid % 64);
    cachePid[slot] = pid; cacheOk[slot] = ok;
    return ok;
}

static const char* pid_name(DWORD pid)
{
    static char name[MAX_PATH];
    typedef BOOL (WINAPI *PFN_Q)(HANDLE, DWORD, LPSTR, PDWORD);
    static PFN_Q q = (PFN_Q)GetProcAddress(GetModuleHandleA("kernel32.dll"), "QueryFullProcessImageNameA");
    lstrcpynA(name, "?", sizeof name);
    HANDLE h = OpenProcess(0x1000, FALSE, pid);
    if (h) { DWORD len = MAX_PATH; if (q && q(h, 0, name, &len)) { const char* b = strrchr(name, '\\'); if (b) memmove(name, b + 1, strlen(b)); } CloseHandle(h); }
    return name;
}

static DWORD WINAPI dbwin_thread(LPVOID p)
{
    DbwinCtx* c = (DbwinCtx*)p;
    long written = 0;
    for (;;) {
        SetEvent(c->ready);
        if (WaitForSingleObject(c->data, INFINITE) != WAIT_OBJECT_0) return 0;
        DWORD pid = *(const DWORD*)c->view;
        char msg[4096 - sizeof(DWORD) + 1];
        memcpy(msg, c->view + sizeof(DWORD), sizeof msg - 1); msg[sizeof msg - 1] = 0;
        if (!pid_is_ours(pid) || written > (8L << 20)) continue;
        size_t n = strlen(msg);
        while (n && (msg[n - 1] == '\n' || msg[n - 1] == '\r')) msg[--n] = 0;
        if (!n) continue;
        SYSTEMTIME t; GetLocalTime(&t);
        written += fprintf(g_dbgFile, "[%02d:%02d:%02d.%03d] %s %lu: %s\n",
                           t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, pid_name(pid), pid, msg);
        fflush(g_dbgFile);                     // a crash or a kill must not lose the last lines
    }
}

static void debug_capture_start(void)
{
    GetLocalTime(&g_sessionStart);
    GetSystemTimeAsFileTime(&g_sessionStartFt);
    HANDLE probe = OpenEventA(SYNCHRONIZE, FALSE, "DBWIN_BUFFER_READY");
    if (probe) { CloseHandle(probe); logf("debug output: another listener (a debugger / DebugView) is running - not captured"); return; }
    static DbwinCtx c;
    HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, "DBWIN_BUFFER");
    c.ready = CreateEventA(NULL, FALSE, FALSE, "DBWIN_BUFFER_READY");
    c.data  = CreateEventA(NULL, FALSE, FALSE, "DBWIN_DATA_READY");
    c.view  = map ? (const char*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0) : NULL;
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%s\\LOG\\debug-output.log", g_root);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > (8u << 20)) DeleteFileA(path);
    g_dbgFile = fopen(path, "a");
    if (!c.view || !c.ready || !c.data || !g_dbgFile) { logf("debug output: could not start the listener (%lu)", GetLastError()); return; }
    fprintf(g_dbgFile, "==== session %04d-%02d-%02d %02d:%02d:%02d ====\n", g_sessionStart.wYear, g_sessionStart.wMonth,
            g_sessionStart.wDay, g_sessionStart.wHour, g_sessionStart.wMinute, g_sessionStart.wSecond);
    fflush(g_dbgFile);
    HANDLE th = CreateThread(NULL, 0, dbwin_thread, &c, 0, NULL);
    if (th) CloseHandle(th);
    logf("debug output: captured to LOG\\debug-output.log");
}

// Runs a console command hidden and returns what it printed (at most ~256 KB).
static std::string run_capture(const char* cmd)
{
    std::string out;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return out;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si = { sizeof(si) }; si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = wr; si.hStdError = wr; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };
    std::string c = cmd;
    if (CreateProcessA(NULL, &c[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(wr); wr = NULL;
        char buf[4096]; DWORD got;
        while (out.size() < 256 * 1024 && ReadFile(rd, buf, sizeof buf, &got, NULL) && got) out.append(buf, got);
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    if (wr) CloseHandle(wr);
    CloseHandle(rd);
    return out;
}

static void session_report(void)
{
    if (g_dbgFile) fflush(g_dbgFile);
    // the race engine's traces of this session -> LOG\race-<name>
    char pat[MAX_PATH]; snprintf(pat, sizeof(pat), "%s\\Game\\LOG\\trace*.txt", g_root);
    WIN32_FIND_DATAA fd; HANDLE f = FindFirstFileA(pat, &fd); int copied = 0;
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (CompareFileTime(&fd.ftLastWriteTime, &g_sessionStartFt) < 0) continue;
            char src[MAX_PATH], dst[MAX_PATH];
            snprintf(src, sizeof(src), "%s\\Game\\LOG\\%s", g_root, fd.cFileName);
            snprintf(dst, sizeof(dst), "%s\\LOG\\race-%s", g_root, fd.cFileName);
            if (CopyFileA(src, dst, FALSE)) ++copied;
        } while (FindNextFileA(f, &fd));
        FindClose(f);
    }
    if (copied) logf("race traces of this session copied to LOG\\race-trace*.txt: %d", copied);
    // DDrawCompat writes its own log next to the menus: copy this session's into LOG, which is what players send
    {
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src, sizeof(src), "%s\\Shell\\bin\\DDrawCompat-AMPlayer.log", g_root);
        snprintf(dst, sizeof(dst), "%s\\LOG\\ddrawcompat-AMPlayer.log", g_root);
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExA(src, GetFileExInfoStandard, &fa) && CompareFileTime(&fa.ftLastWriteTime, &g_sessionStartFt) >= 0 &&
            CopyFileA(src, dst, FALSE))
            logf("DDrawCompat's log of this session copied to LOG\\ddrawcompat-AMPlayer.log");
    }
    // Windows' crash records (they arrive a moment after the exit)
    Sleep(3000);
    SYSTEMTIME now; GetLocalTime(&now);
    FILETIME a, b; SystemTimeToFileTime(&g_sessionStart, &a); SystemTimeToFileTime(&now, &b);
    ULONGLONG ms = ((((ULONGLONG)b.dwHighDateTime << 32) | b.dwLowDateTime) -
                    (((ULONGLONG)a.dwHighDateTime << 32) | a.dwLowDateTime)) / 10000 + 60000;
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "wevtutil.exe qe Application /rd:true /c:40 /f:text \"/q:*[System[(Provider[@Name='Application Error'] "
             "or Provider[@Name='Application Hang'] or Provider[@Name='.NET Runtime'] or Provider[@Name='Windows Error Reporting']) "
             "and TimeCreated[timediff(@SystemTime) <= %llu]]]\"", ms);
    std::string ev = run_capture(cmd);
    int shown = 0;
    for (size_t pos = 0; (pos = ev.find("Event[", pos)) != std::string::npos; ) {
        size_t end = ev.find("Event[", pos + 6);
        std::string one = ev.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? ev.size() : end;
        std::string low = one; for (size_t i = 0; i < low.size(); i++) low[i] = (char)tolower((unsigned char)low[i]);
        if (low.find("amplayer") == std::string::npos && low.find("nascar_gvr") == std::string::npos) continue;
        if (++shown > 6) break;
        logf("windows crash record %d:", shown);
        size_t ls = 0; int lines = 0;
        while (ls < one.size() && lines < 40) {
            size_t le = one.find('\n', ls); if (le == std::string::npos) le = one.size();
            std::string line = one.substr(ls, le - ls);
            while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ')) line.erase(line.size() - 1);
            if (!line.empty()) { logf("    %s", line.c_str()); ++lines; }
            ls = le + 1;
        }
    }
    if (!shown) logf("windows crash records: none for AMPlayer / NASCAR_GVR in this session");
}

// ---------------------------------------------------------------- freeze capture ([Debug] Log)
//
// A front end that never shows a window, or stops answering, leaves nothing in any log (seen on a
// PC where both games stopped before the first plug-in loaded). With [Debug] Log=true the launcher
// watches the program it started and, once per session, when no window has appeared after
// kFreezeNoWindowMs or its window has not answered for kFreezeHungMs, records:
//   * every thread's call stack in nascarlaunch.log - module+offset and, where Windows' DLLs export
//     one, the function name (no symbol server, nothing downloaded);
//   * a small minidump, LOG\freeze-<program>-<pid>.dmp (threads, stacks, module list, handles).
// The program keeps running; the player can still close it.
static bool g_diag = false;
static const DWORD kFreezeNoWindowMs = 20000;
static const DWORD kFreezeHungMs     = 15000;

static void log_thread_stacks(HANDLE proc, DWORD pid)
{
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    if (!SymInitialize(proc, NULL, TRUE)) { logf("freeze: stack walk unavailable (%lu)", GetLastError()); return; }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    int threads = 0;
    for (BOOL ok = Thread32First(snap, &te); ok && threads < 64; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!th) continue;
        ++threads;
        CONTEXT ctx = { 0 }; ctx.ContextFlags = CONTEXT_FULL;
        DWORD sus = SuspendThread(th);
        if (sus != (DWORD)-1 && GetThreadContext(th, &ctx)) {
            logf("freeze: thread %lu", te.th32ThreadID);
            STACKFRAME64 sf = { 0 };
            sf.AddrPC.Offset = ctx.Eip;    sf.AddrPC.Mode = AddrModeFlat;
            sf.AddrFrame.Offset = ctx.Ebp; sf.AddrFrame.Mode = AddrModeFlat;
            sf.AddrStack.Offset = ctx.Esp; sf.AddrStack.Mode = AddrModeFlat;
            for (int n = 0; n < 48 && StackWalk64(IMAGE_FILE_MACHINE_I386, proc, th, &sf, &ctx, NULL,
                                                  SymFunctionTableAccess64, SymGetModuleBase64, NULL); ++n) {
                DWORD64 pc = sf.AddrPC.Offset;
                if (!pc) break;
                IMAGEHLP_MODULE64 mi = { sizeof(mi) };
                char sbuf[sizeof(SYMBOL_INFO) + 256]; SYMBOL_INFO* si = (SYMBOL_INFO*)sbuf;
                si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
                DWORD64 disp = 0;
                bool mod = SymGetModuleInfo64(proc, pc, &mi) != FALSE;
                bool sym = mod && SymFromAddr(proc, pc, &disp, si);
                if (sym)      logf("freeze:     %s+0x%llX  (%s+0x%llX)", mi.ModuleName, pc - mi.BaseOfImage, si->Name, disp);
                else if (mod) logf("freeze:     %s+0x%llX", mi.ModuleName, pc - mi.BaseOfImage);
                else          logf("freeze:     %08llX (no module: generated code, e.g. .NET)", pc);
            }
        }
        if (sus != (DWORD)-1) ResumeThread(th);
        CloseHandle(th);
    }
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    SymCleanup(proc);
    logf("freeze: %d thread(s) recorded", threads);
}

// Which thread owns Windows' DLL-loading lock in the frozen program (PEB+0xA0 -> the lock,
// +0x0C its owner): a start-up freeze is usually one thread holding it while it waits for
// something that needs another thread. Both programs are 32-bit, like this launcher.
static void log_loader_lock(HANDLE proc)
{
    typedef LONG (WINAPI *PFN_NtQIP)(HANDLE, int, PVOID, ULONG, PULONG);
    PFN_NtQIP q = (PFN_NtQIP)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess");
    struct { PVOID r1; PVOID peb; PVOID r2[2]; ULONG_PTR pid; PVOID r3; } pbi = { 0 };
    DWORD lock = 0, owner = 0, count = 0; SIZE_T got = 0;
    if (!q || q(proc, 0 /*ProcessBasicInformation*/, &pbi, sizeof pbi, NULL) < 0 || !pbi.peb ||
        !ReadProcessMemory(proc, (BYTE*)pbi.peb + 0xA0, &lock, 4, &got) || !lock ||
        !ReadProcessMemory(proc, (BYTE*)(ULONG_PTR)lock + 0x0C, &owner, 4, &got) ||
        !ReadProcessMemory(proc, (BYTE*)(ULONG_PTR)lock + 0x04, &count, 4, &got)) {
        logf("freeze: Windows' DLL-loading lock: could not be read (%lu)", GetLastError());
        return;
    }
    if (owner) logf("freeze: Windows' DLL-loading lock is HELD by thread %lu (lock count %ld)", owner, (long)count);
    else       logf("freeze: Windows' DLL-loading lock is free");
}

static void freeze_capture(HANDLE proc, DWORD pid, const char* exe, const char* why)
{
    logf("freeze: %s - recording what it is doing (it keeps running)", why);
    log_loader_lock(proc);
    log_thread_stacks(proc, pid);
    const char* b = strrchr(exe, '\\'); b = b ? b + 1 : exe;
    char name[64]; lstrcpynA(name, b, sizeof name); char* dot = strrchr(name, '.'); if (dot) *dot = 0;
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%s\\LOG\\freeze-%s-%lu.dmp", g_root, name, pid);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { logf("freeze: could not create %s (%lu)", path, GetLastError()); return; }
    BOOL ok = MiniDumpWriteDump(proc, pid, f, (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
                                MiniDumpWithHandleData | MiniDumpWithProcessThreadData), NULL, NULL, NULL);
    DWORD err = GetLastError();
    LARGE_INTEGER sz = { 0 }; GetFileSizeEx(f, &sz);
    CloseHandle(f);
    if (ok) logf("freeze: minidump LOG\\freeze-%s-%lu.dmp (%lld KB)", name, pid, sz.QuadPart / 1024);
    else { logf("freeze: minidump failed (%lu)", err); DeleteFileA(path); }
}

struct FreezeWatch { DWORD start, hungSince; bool sawWindow, done; };

static BOOL CALLBACK visible_window_cb(HWND h, LPARAM lp)
{
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid == ((DWORD*)lp)[0] && IsWindowVisible(h)) { ((HWND*)(((DWORD*)lp) + 1))[0] = h; return FALSE; }
    return TRUE;
}

static void freeze_watch(FreezeWatch& w, HANDLE proc, DWORD pid, const char* exe)
{
    if (!g_diag || w.done) return;
    struct { DWORD pid; HWND h; } q = { pid, NULL };
    EnumWindows(visible_window_cb, (LPARAM)&q);
    DWORD now = GetTickCount();
    char why[160];
    if (!q.h) {
        if (!w.sawWindow && now - w.start >= kFreezeNoWindowMs) {
            snprintf(why, sizeof why, "no window %lu s after the start", kFreezeNoWindowMs / 1000);
            w.done = true; freeze_capture(proc, pid, exe, why);
        }
        w.hungSince = 0;
        return;
    }
    w.sawWindow = true;
    // IsHungAppWindow only reports a window as hung once input waits unanswered - a frozen window
    // nobody clicks stays "fine" (seen 2026-11-11). Ask the window itself: an empty message must
    // be answered within 2 s.
    DWORD_PTR r = 0;
    if (SendMessageTimeoutA(q.h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &r)) { w.hungSince = 0; return; }
    if (!w.hungSince) { w.hungSince = now; return; }
    if (now - w.hungSince >= kFreezeHungMs) {
        snprintf(why, sizeof why, "its window has not answered for %lu s", kFreezeHungMs / 1000);
        w.done = true; freeze_capture(proc, pid, exe, why);
    }
}

// Runs on its own thread: the poll loop in run() tidies the game's windows with SetWindowPos, which
// waits for a frozen window just like the player does - a watch inside that loop never fired.
struct FreezeThreadCtx { HANDLE proc; DWORD pid; char exe[MAX_PATH]; };
static DWORD WINAPI freeze_thread(LPVOID p)
{
    FreezeThreadCtx* c = (FreezeThreadCtx*)p;
    FreezeWatch w = { GetTickCount(), 0, false, false };
    while (!w.done && WaitForSingleObject(c->proc, 250) == WAIT_TIMEOUT) freeze_watch(w, c->proc, c->pid, c->exe);
    CloseHandle(c->proc);
    delete c;
    return 0;
}

static void freeze_watch_start(HANDLE proc, DWORD pid, const char* exe)
{
    if (!g_diag) return;
    FreezeThreadCtx* c = new FreezeThreadCtx;
    c->pid = pid; lstrcpynA(c->exe, exe, MAX_PATH);
    if (!DuplicateHandle(GetCurrentProcess(), proc, GetCurrentProcess(), &c->proc, 0, FALSE, DUPLICATE_SAME_ACCESS)) { delete c; return; }
    HANDLE th = CreateThread(NULL, 0, freeze_thread, c, 0, NULL);
    if (th) CloseHandle(th); else { CloseHandle(c->proc); delete c; }
}

// ---------------------------------------------------------------- Smart App Control / Code Integrity
//
// Windows 11's Smart App Control (and company code-integrity policies) can refuse to load the game's unsigned
// files. The program then fails with no message of its own. After each session the launcher reads Windows'
// Code Integrity log for blocks of files inside the install folder, logs them, and explains them once.
static const char* sac_state_text(void)
{
    DWORD v = 0, n = sizeof(v);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\CI\\Policy",
                     "VerifiedAndReputablePolicyState", RRF_RT_REG_DWORD, NULL, &v, &n) != ERROR_SUCCESS) return "not on this Windows";
    return v == 0 ? "off" : v == 1 ? "ON (blocks unsigned programs)" : v == 2 ? "evaluation (watches, does not block)" : "unknown state";
}

static void check_blocked_files(ULONGLONG sessionMs)
{
    // Windows names files as \Device\HarddiskVolumeN\...: translate the install folder's drive the same way
    char drive[3] = { g_root[0], ':', 0 }, dev[MAX_PATH] = "";
    if (g_root[1] != ':' || !QueryDosDeviceA(drive, dev, sizeof(dev))) return;
    std::string want = lower_str(std::string(dev) + (g_root + 2)) + "\\";
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "wevtutil.exe qe Microsoft-Windows-CodeIntegrity/Operational /rd:true /c:60 /f:text "
             "\"/q:*[System[(EventID=3033 or EventID=3034 or EventID=3076 or EventID=3077) and TimeCreated[timediff(@SystemTime) <= %llu]]]\"",
             sessionMs + 60000);
    std::string ev = run_capture(cmd);
    std::string firstBlocked;
    std::vector<std::pair<std::string, int> > files;      // "BLOCKED x.dll" -> how often
    int shown = 0;
    for (size_t pos = 0; (pos = ev.find("Event[", pos)) != std::string::npos; ) {
        size_t end = ev.find("Event[", pos + 6);
        std::string one = ev.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? ev.size() : end;
        std::string low = lower_str(one);
        size_t at = low.find(want);
        size_t load = low.find("attempted to load ");
        if (at == std::string::npos || load == std::string::npos) continue;
        size_t fs = load + 18, fe = low.find(" that ", fs);
        std::string file = one.substr(fs, fe == std::string::npos ? std::string::npos : fe - fs);
        if (lower_str(file).find(want) == 0) file = file.substr(want.size());      // relative to the install folder
        bool blocked = low.find("event id: 3033") != std::string::npos || low.find("event id: 3077") != std::string::npos;
        std::string key = std::string(blocked ? "BLOCKED " : "would block (audit only) ") + file;
        size_t k = 0;
        while (k < files.size() && files[k].first != key) k++;
        if (k == files.size()) files.push_back(std::make_pair(key, 0));
        files[k].second++;
        ++shown;
        if (blocked && firstBlocked.empty()) firstBlocked = file;
    }
    if (!shown) return;
    for (size_t k = 0; k < files.size() && k < 8; k++)
        logf("windows code integrity: %s (%d time%s)", files[k].first.c_str(), files[k].second, files[k].second == 1 ? "" : "s");
    logf("windows code integrity: Smart App Control is %s", sac_state_text());
    if (firstBlocked.empty()) return;
    std::string msg = "Windows blocked a file of the game, so it could not run properly:\n\n    " + firstBlocked +
        "\n\nThis is done by Windows' Smart App Control (or a company security policy) because the game's files are "
        "not signed. Smart App Control is in Windows Security > App & browser control.\n\n"
        "Note: once Smart App Control is turned off, Windows can only turn it on again by reinstalling Windows.";
    MessageBoxA(NULL, msg.c_str(), g_title, MB_ICONWARNING | MB_OK);
}

static DWORD run(const char* exe, const char* args, const char* cwd, bool wait, const Settings& s,
                 const char* preload = NULL, const char* selfLoadFallback = NULL)
{
    ULONGLONG started = GetTickCount64();
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
    if (selfLoadFallback) {
        // Shell\bin\glu32.dll signals once the menus loaded the shim by themselves. Without that signal
        // (the helper skipped or blocked), start the menus again the older way, with injection.
        char name[64];
        snprintf(name, sizeof(name), "Local\\GvrGlu_loaded_%lu", pi.dwProcessId);
        bool ok = false;
        for (int i = 0; i < 100 && !ok; i++) {
            HANDLE e = OpenEventA(SYNCHRONIZE, FALSE, name);
            if (e) { CloseHandle(e); ok = true; break; }
            if (WaitForSingleObject(pi.hProcess, 100) != WAIT_TIMEOUT) break;
        }
        if (ok) logf("shim: loaded by the menus themselves");
        else {
            logf("shim: the menus did not load GvrIO.dll themselves within 10 s - starting them again with injection");
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            return run(exe, args, cwd, wait, s, selfLoadFallback, NULL);
        }
    }
    if (wait) freeze_watch_start(pi.hProcess, pi.dwProcessId, exe);
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
    if (wait) { restore_taskbar(); check_blocked_files(GetTickCount64() - started); }
    return code;
}

// ---------------------------------------------------------------- main

// [Display] HudText: the race places its HUD pictures as fractions of the screen, but draws its text with fonts of a
// fixed point size (Game\GameData\HUD\HUD.ini, "elem=FONT ... SIZE=n", made for the arcade's 768-pixel-high screen).
// So the text shrank on bigger screens. At every start the launcher writes HUD.ini from the original (kept as
// HUD.ini.oem) with each font SIZE times the race height / 768. The race renders each new size once and keeps it
// as "<font>-<size>-400.bmp/.fnt" next to HUD.ini (tested up to 169 points: sharp, all characters).
// auto = grow with the screen, original = the arcade's sizes, or a number such as 1.5 = that many times the arcade's.
static void apply_hud_text(const Settings& s)
{
    char hud[MAX_PATH], oem[MAX_PATH];
    snprintf(hud, sizeof hud, "%s\\Game\\GameData\\HUD\\HUD.ini", g_root);
    snprintf(oem, sizeof oem, "%s.oem", hud);
    if (!PathFileExistsA(hud)) return;
    if (!PathFileExistsA(oem) && !CopyFileA(hud, oem, TRUE)) { logf("hud text: could not keep the original HUD.ini (%lu)", GetLastError()); return; }
    std::string want = lower_str(ini_str("Display", "HudText", "auto"));
    double f = 1.0;
    if (want == "auto" || want.empty()) f = s.height / 768.0;
    else if (want != "original") {
        char* end = NULL; double v = strtod(want.c_str(), &end);
        if (end && !*end && v >= 0.25 && v <= 4.0) f = v;
        else { logf("hud text: [Display] HudText = '%s' is not auto, original or a number from 0.25 to 4 - using auto", want.c_str()); f = s.height / 768.0; }
    }
    if (f > 4.0) f = 4.0;
    std::string text, cur;
    if (!read_whole(oem, text)) return;
    read_whole(hud, cur);
    // each "elem=FONT" line: SIZE=n -> SIZE=round(n*f)
    std::string out; out.reserve(text.size() + 64);
    int changed = 0;
    for (size_t pos = 0; pos < text.size(); ) {
        size_t eol = text.find('\n', pos); if (eol == std::string::npos) eol = text.size(); else eol++;
        std::string line = text.substr(pos, eol - pos);
        pos = eol;
        if (line.compare(0, 9, "elem=FONT") == 0) {
            size_t k = line.find("SIZE=");
            if (k != std::string::npos) {
                size_t d = k + 5, e = d;
                while (e < line.size() && isdigit((unsigned char)line[e])) e++;
                if (e > d) {
                    int n = atoi(line.substr(d, e - d).c_str());
                    int m = (int)(n * f + 0.5); if (m < 8) m = 8;
                    if (m != n) changed++;
                    char num[16]; snprintf(num, sizeof num, "%d", m);
                    line = line.substr(0, d) + num + line.substr(e);
                }
            }
        }
        out += line;
    }
    if (out == cur) return;
    FILE* fp = fopen(hud, "wb");
    if (!fp) { logf("hud text: could not write HUD.ini (%lu)", GetLastError()); return; }
    fwrite(out.data(), 1, out.size(), fp);
    fclose(fp);
    logf("hud text: race fonts x%.2f for a %d-pixel-high race (%d sizes changed; [Display] HudText = %s)", f, s.height, changed, want.c_str());
}

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
        char rt[MAX_PATH];
        if (s.mode != "race" && !dotnet11_installed() && !private_runtime(rt, sizeof(rt)))
            die("%s is not fully set up on this PC.\n\n"
                "The copy of Microsoft .NET 1.1 that the menus use (the Runtime folder in the game folder)\n"
                "is missing. Run Setup (or Install.bat) again - it puts it back. Copying the game folder\n"
                "by hand from another PC is not enough.", g_title);
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

// ---------------------------------------------------------------- multiplayer glue (NascarNet.cpp)

static const Settings* g_mpSettings;
static char g_mpGameExe[MAX_PATH], g_mpGameDir[MAX_PATH];

// everything after "-mode multi ...": the host's track/series/laps, this player's own car and display
static std::string mp_race_args(const std::string& track, const std::string& series, int laps)
{
    const Settings& s = *g_mpSettings;
    std::string seriesArg;
    if (!series.empty() && series_installed(series, g_mpGameDir)) seriesArg = "-series " + series + " ";
    char args[1024];
    snprintf(args, sizeof(args), "-track %s %s-car %s -laps %d %s -width %d -height %d %s",
             track.c_str(), seriesArg.c_str(), s.car.c_str(), laps,
             s.fullscreen ? "-fullscreen" : "-windowed", s.width, s.height, s.extraArgs.c_str());
    return std::string(args);
}

static unsigned long mp_launch(const std::string& args)
{
    if (!PathFileExistsA(g_mpGameExe)) die("NASCAR_GVR.exe was not found:\n%s", g_mpGameExe);
    DWORD code = run(g_mpGameExe, args.c_str(), g_mpGameDir, true, *g_mpSettings);   // the relay agent lives as long as the race
    if (g_diag) session_report();
    return code;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR cmdline, int)
{
    check_folder_name();                       // before any ANSI file name is used
    GetModuleFileNameA(NULL, g_root, MAX_PATH);
    PathRemoveFileSpecA(g_root);
    snprintf(g_ini, sizeof(g_ini), "%s\\nascar_settings.ini", g_root);
    {
        char amp[MAX_PATH];
        snprintf(amp, sizeof(amp), "%s\\Shell\\bin\\AMPlayer.exe", g_root);
        if (PathFileExistsA(amp)) strcpy(g_title, is_teamracing_shell(amp) ? "NASCAR Team Racing" : "NASCAR Racing");
    }
    // developer override (two players on one PC for tests): --settings <file> reads another settings file
    if (const char* so = strstr(cmdline, "--settings ")) {
        so += 11;
        while (*so == ' ') so++;
        bool q = *so == '"'; if (q) so++;
        size_t n = 0;
        while (so[n] && (q ? so[n] != '"' : so[n] != ' ') && n < MAX_PATH - 1) n++;
        memcpy(g_ini, so, n); g_ini[n] = 0;
    }
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
    // [Display] Gpu before any graphics test. When the launcher's own chip changed, start it once more so the
    // tests below run on the chip the menus will get (Windows applies the choice when a program starts).
    if (apply_gpu_preference() && !GetEnvironmentVariableA("GVRLAUNCH_GPU_RESTARTED", NULL, 0)) {
        SetEnvironmentVariableA("GVRLAUNCH_GPU_RESTARTED", "1");
        std::string self = GetCommandLineA();
        STARTUPINFOA rsi = { sizeof(rsi) }; PROCESS_INFORMATION rpi = { 0 };
        if (CreateProcessA(NULL, &self[0], NULL, NULL, FALSE, 0, NULL, NULL, &rsi, &rpi)) {
            logf("gpu: restarting the launcher once so it runs on the chosen graphics chip");
            CloseHandle(rpi.hThread); CloseHandle(rpi.hProcess);
            return 0;
        }
    }
    check_install(s);
    std::string mpMode = ini_str("Multiplayer", "Mode", "off");
    for (size_t i = 0; i < mpMode.size(); i++) mpMode[i] = (char)tolower((unsigned char)mpMode[i]);
    const bool mp = mpMode == "host" || mpMode == "join";

    apply_settings(s);
    apply_car_detail();
    apply_race_graphics();
    apply_hud_text(s);
    ensure_anark_prefs();

    char gameDir[MAX_PATH], shellDir[MAX_PATH], amPlayer[MAX_PATH], shellAm[MAX_PATH], gameExe[MAX_PATH];
    snprintf(gameDir,  sizeof(gameDir),  "%s\\Game", g_root);
    snprintf(shellDir, sizeof(shellDir), "%s\\Shell", g_root);
    snprintf(amPlayer, sizeof(amPlayer), "%s\\bin\\AMPlayer.exe", shellDir);
    snprintf(shellAm,  sizeof(shellAm),  "%s\\Shell.am", shellDir);
    snprintf(gameExe,  sizeof(gameExe),  "%s\\NASCAR_GVR.exe", gameDir);

    // before use_private_runtime: the builder is a .NET 4 program, and the COMPLUS_* variables that
    // select the private .NET 1.1 for the menus would make it fail ("mscorwks.dll could not be loaded")
    if (s.mode != "race" && !mp) build_driver_art(s, amPlayer);
    if (s.mode != "race" && !mp) apply_link_plugin(mpMode == "lobby", amPlayer);

    ensure_db_env();
    use_private_runtime();

    choose_teamracing_renderer(amPlayer);
    choose_classic_renderer(amPlayer);
    const bool diag = ini_bool("Debug", "Log", false);
    g_diag = diag;                             // also arms the freeze capture in run()
    if (diag) debug_capture_start();
    logf("smart app control: %s", sac_state_text());

    if (mp) {
        // [Multiplayer]: a linked race, straight in (no menus); NascarNet.cpp does the lobby and relay
        NetSettings ns;
        ns.mode        = mpMode;
        ns.relay       = ini_str("Multiplayer", "Relay", "");
        ns.room        = ini_str("Multiplayer", "Room", "");
        ns.hostAddress = ini_str("Multiplayer", "HostAddress", "");
        ns.players     = ini_int("Multiplayer", "Players", 2);
        ns.track = s.track; ns.series = s.series; ns.laps = s.laps;
        g_mpSettings = &s;
        strcpy(g_mpGameExe, gameExe); strcpy(g_mpGameDir, gameDir);
        return net_run(ns, mp_race_args, mp_launch, logf);
    }

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
        if (diag && (s.waitForExit || s.borderless)) session_report();
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
    // Shell\bin\glu32.dll (ours) makes the menus load the shim by themselves, before their first instruction:
    // no injection, which some antivirus programs block. The older injection is the fallback, and
    // [Launcher] Preload = inject forces it (support).
    char glu[MAX_PATH]; std::string gbin;
    snprintf(glu, sizeof(glu), "%s\\glu32.dll", binDir);
    bool selfLoad = read_whole(glu, gbin) && gbin.find("GvrGlu: loads GvrIO.dll into the menus") != std::string::npos;
    std::string how = lower_str(ini_str("Launcher", "Preload", "auto"));
    if (how == "inject") selfLoad = false;
    if (PathFileExistsA(shim))
        logf(selfLoad ? "shim: the menus load GvrIO.dll themselves (Shell\\bin\\glu32.dll)"
                      : "shim: injecting GvrIO.dll into the menus%s", how == "inject" ? " ([Launcher] Preload = inject)" : "");
    bool haveShim = PathFileExistsA(shim) != FALSE;
    run(amPlayer, args, binDir, s.waitForExit || s.borderless, s, haveShim && !selfLoad ? shim : NULL,
        haveShim && selfLoad ? shim : NULL);
    if (diag && (s.waitForExit || s.borderless)) session_report();
    return 0;
}
