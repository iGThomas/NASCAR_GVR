// ============================================================================
//  GvrIOReg.cpp  -  NASCAR's private registry (part of the GvrIO.dll shim)
//
//  The engine is the shared GvrPrivReg library (GIT\src\GvrPrivReg); this file
//  only says what NASCAR's registry looks like. Every registry call for
//  HKLM\SOFTWARE\GlobalVR or HKLM\SOFTWARE\gvr is answered from
//  <install>\nascar_registry.ini - see NASCAR_FINDINGS.md section 15:
//    * folder values (launchFolder, GameRoot, PlusSchemaPath, ...) are computed
//      from the install folder (%ROOT%), so a moved install just works;
//    * the OEM values below fill in anything the file does not have;
//    * the front end and the race write their hand-over values to the file.
//  NascarLaunch.exe loads this DLL into AMPlayer before AMPlayer's own code
//  (it reads GameRoot before loading plug-ins); the race imports it.
//  GVRIOSHIM_NO_PRIVATE_REGISTRY=1 turns it off.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "GvrIOPad.h"
#include "GvrPrivReg.h"

static const char* const kRoots[] = { "GlobalVR", "gvr" };

static const PrivRegDefault kDefaults[] = {
    // identity + display (NASCAR.reg)
    { "GlobalVR\\NASCAR", "Version", "1.1.0" },
    { "GlobalVR\\NASCAR", "Build", "163" },
    { "GlobalVR\\NASCAR", "CommVersion", "1.1.0" },
    { "GlobalVR\\NASCAR", "Prefix", "" },
    { "GlobalVR\\NASCAR", "Suffix", "" },
    { "GlobalVR\\NASCAR", "FullScreenWidth", "1360" },
    { "GlobalVR\\NASCAR", "FullScreenHeight", "768" },
    { "GlobalVR\\NASCAR", "FourButtonAbort", "1" },
    { "GlobalVR\\NASCAR", "StallMonitorTest", "0" },
    // 1 = cabinet first boot: Shell.am then shows "Calibrate the pedals" (NASCAR_FINDINGS 16)
    { "GlobalVR\\NASCAR", "FirstTimeStartup", "0" },
    // launch wiring (NASCAR_Shell.reg)
    { "GlobalVR\\NASCAR", "cmd", "-startPos 43 " },
    { "GlobalVR\\NASCAR", "wtf", "" },
    { "GlobalVR\\NASCAR", "launchName", "NASCAR_GVR.exe" },
    { "GlobalVR\\NASCAR", "launchFolder", "%ROOT%\\Game" },
    { "GlobalVR\\NASCAR", "attractName", "NASCAR_Attract.am" },
    { "GlobalVR\\NASCAR", "attractFolder", "%ROOT%\\Shell" },
    { "GlobalVR\\NASCAR", "selectName", "NASCAR_Selection.am" },
    { "GlobalVR\\NASCAR", "selectFolder", "%ROOT%\\Shell" },
    { "GlobalVR\\NASCAR", "AttractAudioCounter", "0" },
    { "GlobalVR\\NASCAR", "MusicVolume", "70" },
    { "GlobalVR\\NASCAR", "SimpleAttract", "0" },
    // never empty: the shell's startup check parses them (NASCAR_FINDINGS 11.4)
    { "GlobalVR\\NASCAR", "UpTimeStart", "%NOW%" },
    { "GlobalVR\\NASCAR", "UpTimeEnd", "%NOW%" },
    // GvrIO / GvrParseXml: <GameRoot>\Config\GvrIO.xml and the shell's plug-in list
    { "gvr\\Plus\\2.0\\Cabinet", "GameRoot", "%ROOT%\\Game\\" },
    // the database (Schema.reg); the SQLite provider walks up from PlusSchemaPath
    { "gvr\\Plus\\1.1\\Cabinet", "PlusSchemaPath", "%ROOT%\\GVR\\GvrPlus\\4\\schema\\NASCARcabinetXml.enc" },
    { "gvr\\Plus\\1.1\\Cabinet", "PublicKeyPath", "%ROOT%\\GVR\\GvrPlus\\4\\key\\publickey.xml" },
    { "gvr\\Plus\\1.1\\Cabinet", "PromotionPath", "%ROOT%\\GVR\\GvrPlus\\4\\promotions" },
    { "gvr\\Plus\\1.1\\Cabinet", "MaxObjectLoadCount", "300" },
};

static const PrivRegConfig kNascar = {
    "nascar_registry.ini",
    kRoots, (int)(sizeof(kRoots) / sizeof(kRoots[0])),
    kDefaults, (int)(sizeof(kDefaults) / sizeof(kDefaults[0])),
    true,   // any key under the roots opens (the scripts read keys the OEM never created)
};

void reg_attach(void)
{
    // which build is running - a user's log otherwise cannot tell a hotfix from the release
    shim_log("shim build: %s %s (private registry: lock only on game keys, RegTrace)", __DATE__, __TIME__);
    char ini[MAX_PATH];
    if (!shim_settings_path(ini, sizeof ini)) return;    // no install root: leave the real registry
    // [Debug] RegTrace=true: log every registry call to <install>\LOG\regtrace-<exe>-<pid>.log
    char v[16] = "";
    GetPrivateProfileStringA("Debug", "RegTrace", "", v, sizeof v, ini);
    char* s = strrchr(ini, '\\');
    if (s) *s = 0;                                         // <install>
    if (_stricmp(v, "true") == 0 || strcmp(v, "1") == 0) {
        char exe[MAX_PATH] = "", path[MAX_PATH];
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        char* b = strrchr(exe, '\\');
        b = b ? b + 1 : exe;
        char* dot = strrchr(b, '.');
        if (dot) *dot = 0;
        _snprintf(path, sizeof path, "%s\\LOG", ini);
        path[sizeof path - 1] = 0;
        CreateDirectoryA(path, NULL);
        _snprintf(path, sizeof path, "%s\\LOG\\regtrace-%s-%lu.log", ini, b, GetCurrentProcessId());
        path[sizeof path - 1] = 0;
        privreg_trace(path);
        shim_log("registry: call trace -> %s", path);
    }
    privreg_attach(ini, &kNascar, shim_log);
}
