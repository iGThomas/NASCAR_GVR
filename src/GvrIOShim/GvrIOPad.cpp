// ============================================================================
//  GvrIOPad.cpp  -  Xbox and PlayStation controller support for NASCAR
//                   (compiled into the GvrIO.dll shim, see GvrIOShim.cpp)
//
//  Same pads and the same layout as NFS Underground's GvrInputEmu: an Xbox pad
//  (or anything Steam Input presents as one) through XInput, and a PS4
//  DualShock 4 through raw HID, over USB or Bluetooth.
//
//  WHERE THE PAD PLUGS IN  (NASCAR_FINDINGS.md section 14)
//  The cabinet reaches BOTH programs - the AMPlayer front end and the
//  NASCAR_GVR.exe race - through GvrIO alone. The race never opens a
//  DirectInput joystick (c_dfDIJoystick is linked but unreferenced), so its
//  .CTL "controller 1" bindings are not how a cabinet drives it:
//
//    * The analog wheel and pedals are GvrIOMessageSend msg 5 on device 1,
//      +8 = axis: 0 steering (signed, about -127..127), 1 gas, 2 brake (0..255).
//      The front end reads them as Gvr.AxisX/Y/Z; the race (FUN_0045c650 /
//      0x45a420) uses any NONZERO value in place of its own keyboard input.
//    * Every cabinet button, and the 4-position H-shifter, is a NAME in
//      Game\config\GvrIO.xml. GvrIO reads its own DirectInput keyboard
//      (GetDeviceState, 256 bytes, inside the per-frame GvrIOMessageSend) and a
//      held <key char=".."> becomes that button: GvrIO raises its events
//      (onGvrStartPressed, onGvrViewButtonDown, ...) and answers the race's
//      "is Shifter1..4 held" query (msg 0x42) from the same state.
//
//  So the sticks and triggers answer msg 5 (pad_axis), and the buttons are
//  OR-ed into the keyboard state GvrIO reads. Only GvrIO's OWN keyboard object
//  gets them: GvrIO_oem.dll's DirectInput8Create import is redirected to learn
//  which IDirectInput8 is GvrIO's, and the CreateDevice / GetDeviceState slots of
//  DirectInput's vtables are redirected to hooks that act only for GvrIO's
//  objects (the objects themselves must keep DirectInput's vtable pointer - see
//  the hooks). The race's own DirectInput keyboard, and every .CTL key binding
//  with it, never sees a pad press. A pad button is therefore exactly the cabinet
//  key it stands for, in both programs, and GvrIO does all of the event work.
//
//  The scan code for each cabinet letter comes from MapVirtualKeyA - GvrIO turns
//  scan codes back into letters with the same call - so it follows the keyboard
//  layout (AZERTY included).
//
//  The numpad Swingman camera has no cabinet button, so for the right stick the
//  same GetDeviceState hook holds raw scan codes on the RACE's own keyboard
//  (inject_race_keys): numpad 4/6/8/2.
//
//  Map: nascar_settings.ini [Controller] (race) and [Frontend] (menus). The
//  defaults below put every function on the button NFSU uses for it.
//  GVRIOSHIM_NO_PAD=1 turns all of this off.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "GvrIOPad.h"

static HMODULE g_self    = NULL;
static bool    g_isShell = false;
static bool    g_off     = false;          // GVRIOSHIM_NO_PAD=1
static CRITICAL_SECTION g_cs;              // pad state and the DS4 handle
static volatile LONG g_started = 0, g_ready = 0;

// ------------------------------------------------------------------ pad buttons
// Source-agnostic, identical to GvrInputEmu: an Xbox pad is normalised onto the
// same POSITIONS (A = Cross, B = Circle, X = Square, Y = Triangle, ...).
enum {
    UB_CROSS = 1u<<0, UB_CIRCLE = 1u<<1, UB_SQUARE = 1u<<2, UB_TRIANGLE = 1u<<3,
    UB_L1 = 1u<<4, UB_R1 = 1u<<5, UB_L2 = 1u<<6, UB_R2 = 1u<<7,
    UB_SHARE = 1u<<8, UB_OPTIONS = 1u<<9, UB_L3 = 1u<<10, UB_R3 = 1u<<11,
    UB_PS = 1u<<12, UB_DUP = 1u<<13, UB_DDOWN = 1u<<14, UB_DLEFT = 1u<<15, UB_DRIGHT = 1u<<16,
};

// steer / rx / ry: -127..127 (ry > 0 = stick pushed up); gas / brake: 0..255
struct PadState { bool present; int steer; int gas; int brake; int rx; int ry; unsigned btn; };
static PadState g_pad;

// steering -> -127..127 with the dead zone cut out (no jump at its edge)
static int scale_stick(int v, int deadzone, int full)
{
    int a = v < 0 ? -v : v;
    if (a <= deadzone) return 0;
    int s = (a - deadzone) * 127 / (full - deadzone);
    if (s > 127) s = 127;
    return v < 0 ? -s : s;
}
// trigger -> 0..255; a resting trigger must read exactly 0, because the race takes
// ANY nonzero pedal value over the keyboard
static int scale_trigger(int t)
{
    if (t <= 8) return 0;
    int s = (t - 8) * 255 / (255 - 8);
    return s > 255 ? 255 : s;
}

// ------------------------------------------------------------------ XInput (Xbox)
// Loaded dynamically so the DLL still loads where XInput is absent.
struct XINPUT_GAMEPAD_ { WORD wButtons; BYTE bLeftTrigger, bRightTrigger;
                         SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; };
struct XINPUT_STATE_   { DWORD dwPacketNumber; XINPUT_GAMEPAD_ Gamepad; };
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE_*);
static PFN_XInputGetState pXInputGetState = NULL;
// The connected XInput user index, found by the discovery thread. XInputGetState on an
// EMPTY slot re-enumerates devices on every call (NFSU's 100%-CPU "race-end hang"), so the
// game threads only ever ask a slot that is known to be connected.
static volatile LONG g_xiSlot = -1;

static void load_xinput(void)
{
    const char* names[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (int i = 0; i < 3; ++i) {
        HMODULE h = LoadLibraryA(names[i]);
        if (!h) continue;
        pXInputGetState = (PFN_XInputGetState)GetProcAddress(h, "XInputGetState");
        if (pXInputGetState) { shim_log("pad: XInput via %s", names[i]); return; }
    }
    shim_log("pad: XInput not available");
}

// ------------------------------------------------------------------ DS4 (raw HID)
static HANDLE     g_ds4 = INVALID_HANDLE_VALUE;
static OVERLAPPED g_ov;
static HANDLE     g_ev = NULL;
static bool       g_readPending = false;
// HID ReadFile must be given the collection's InputReportByteLength: USB = 64,
// Bluetooth much larger (547 seen). A short buffer makes the Bluetooth read fail.
static BYTE       g_rpt[1024];
static DWORD      g_rptLen = 78;
static int        g_ds4Steer = 0, g_ds4Gas = 0, g_ds4Brake = 0, g_ds4Rx = 0, g_ds4Ry = 0;
static unsigned   g_ds4Btn = 0;

// Runs on the discovery thread. A DS4 exposes several HID top-level collections; open the
// GAMEPAD one (usage page 1, usage 5/4), falling back to the first Sony interface.
static void open_ds4(void)
{
    GUID hidGuid; HidD_GetHidGuid(&hidGuid);
    HDEVINFO di = SetupDiGetClassDevsA(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return;
    HANDLE found = INVALID_HANDLE_VALUE, fallback = INVALID_HANDLE_VALUE;
    DWORD foundLen = 78, fallbackLen = 78;

    SP_DEVICE_INTERFACE_DATA ifd; ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(di, NULL, &hidGuid, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailA(di, &ifd, NULL, 0, &need, NULL);
        if (!need) continue;
        SP_DEVICE_INTERFACE_DETAIL_DATA_A* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)malloc(need);
        if (!det) continue;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (SetupDiGetDeviceInterfaceDetailA(di, &ifd, det, need, NULL, NULL)) {
            HANDLE h = CreateFileA(det->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES at; at.Size = sizeof(at);
                if (HidD_GetAttributes(h, &at) && at.VendorID == 0x054C) {
                    unsigned inLen = 0, page = 0, usage = 0;
                    PHIDP_PREPARSED_DATA pp = NULL;
                    if (HidD_GetPreparsedData(h, &pp)) {
                        HIDP_CAPS caps;
                        if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                            inLen = caps.InputReportByteLength; page = caps.UsagePage; usage = caps.Usage;
                        }
                        HidD_FreePreparsedData(pp);
                    }
                    DWORD len = (inLen > 0 && inLen <= sizeof(g_rpt)) ? inLen : 78;
                    if (page == 0x01 && (usage == 0x05 || usage == 0x04)) {
                        found = h; foundLen = len; h = INVALID_HANDLE_VALUE;
                        shim_log("pad: PlayStation controller VID 054C PID %04X, report %lu bytes", at.ProductID, len);
                    } else if (fallback == INVALID_HANDLE_VALUE) {
                        fallback = h; fallbackLen = len; h = INVALID_HANDLE_VALUE;
                    }
                }
                if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            }
        }
        free(det);
    }
    SetupDiDestroyDeviceInfoList(di);

    if (found == INVALID_HANDLE_VALUE && fallback != INVALID_HANDLE_VALUE) {
        found = fallback; foundLen = fallbackLen; fallback = INVALID_HANDLE_VALUE;
        shim_log("pad: Sony HID interface without gamepad usage - using it as a fallback");
    }
    if (fallback != INVALID_HANDLE_VALUE) CloseHandle(fallback);
    if (found == INVALID_HANDLE_VALUE) return;

    // Bluetooth DS4 sends only the basic report until feature 0x02 is read; that read
    // switches it to the full report (id 0x11) the decoder needs. Harmless over USB.
    BYTE feat[64] = { 0x02 };
    HidD_GetFeature(found, feat, sizeof(feat));

    EnterCriticalSection(&g_cs);
    g_ds4 = found; g_rptLen = foundLen; g_readPending = false;
    memset(&g_ov, 0, sizeof g_ov); g_ov.hEvent = g_ev;
    g_ds4Steer = 0; g_ds4Gas = 0; g_ds4Brake = 0; g_ds4Rx = 0; g_ds4Ry = 0; g_ds4Btn = 0;
    LeaveCriticalSection(&g_cs);
}

// One DS4 input report -> latched state. USB: report 0x01, payload at +1.
// Bluetooth: report 0x11, the same block 2 bytes later.
static void latch_ds4(DWORD len)
{
    int o;
    if (g_rpt[0] == 0x11)      o = 2;
    else if (g_rpt[0] == 0x01) o = 0;
    else return;
    if ((DWORD)(o + 10) > len) return;

    g_ds4Steer = scale_stick((int)g_rpt[o + 1] - 128, 8, 127);   // left stick X
    g_ds4Rx    = scale_stick((int)g_rpt[o + 3] - 128, 8, 127);   // right stick X
    g_ds4Ry    = scale_stick(128 - (int)g_rpt[o + 4], 8, 127);   // right stick Y (HID: 0 = up)
    g_ds4Brake = scale_trigger(g_rpt[o + 8]);                    // L2
    g_ds4Gas   = scale_trigger(g_rpt[o + 9]);                    // R2

    // byte 5 = hat (low nibble) + face buttons, byte 6 = shoulders/sticks, byte 7 = PS
    int b5 = g_rpt[o + 5], b6 = g_rpt[o + 6], b7 = g_rpt[o + 7], hat = b5 & 0x0F;
    unsigned ub = 0;
    if (b5 & 0x10) ub |= UB_SQUARE;   if (b5 & 0x20) ub |= UB_CROSS;
    if (b5 & 0x40) ub |= UB_CIRCLE;   if (b5 & 0x80) ub |= UB_TRIANGLE;
    if (b6 & 0x01) ub |= UB_L1;       if (b6 & 0x02) ub |= UB_R1;
    if (b6 & 0x04) ub |= UB_L2;       if (b6 & 0x08) ub |= UB_R2;
    if (b6 & 0x10) ub |= UB_SHARE;    if (b6 & 0x20) ub |= UB_OPTIONS;
    if (b6 & 0x40) ub |= UB_L3;       if (b6 & 0x80) ub |= UB_R3;
    if (b7 & 0x01) ub |= UB_PS;
    if (hat == 0 || hat == 1 || hat == 7) ub |= UB_DUP;
    if (hat == 1 || hat == 2 || hat == 3) ub |= UB_DRIGHT;
    if (hat == 3 || hat == 4 || hat == 5) ub |= UB_DDOWN;
    if (hat == 5 || hat == 6 || hat == 7) ub |= UB_DLEFT;
    g_ds4Btn = ub;
}

static void close_ds4(const char* why)
{
    CancelIo(g_ds4);
    CloseHandle(g_ds4);
    g_ds4 = INVALID_HANDLE_VALUE; g_readPending = false;
    shim_log("pad: PlayStation controller closed (%s)", why);
}

// Non-blocking overlapped pump, under g_cs: drain finished reports, keep one read pending.
static void pump_ds4(void)
{
    for (int guard = 0; guard < 16 && g_ds4 != INVALID_HANDLE_VALUE; ++guard) {
        if (g_readPending) {
            DWORD rd = 0;
            if (!GetOverlappedResult(g_ds4, &g_ov, &rd, FALSE)) {
                if (GetLastError() == ERROR_IO_INCOMPLETE) return;      // still waiting
                close_ds4("read failed - unplugged?");
                return;
            }
            g_readPending = false;
            if (rd >= 10) latch_ds4(rd);
        }
        ResetEvent(g_ev);
        DWORD rd = 0;
        if (ReadFile(g_ds4, g_rpt, g_rptLen, &rd, &g_ov)) {
            if (rd >= 10) latch_ds4(rd);        // completed at once; loop for a fresher one
        } else if (GetLastError() == ERROR_IO_PENDING) {
            g_readPending = true;
            return;
        } else {
            close_ds4("device gone");
            return;
        }
    }
}

// Finds pads in the background (and again after an unplug), so the game threads never pay
// for an XInput slot scan or a HID enumeration.
static DWORD WINAPI discovery_thread(LPVOID)
{
    for (;;) {
        if (g_xiSlot < 0 && pXInputGetState) {
            for (DWORD i = 0; i < 4; ++i) {
                XINPUT_STATE_ xs;
                if (pXInputGetState(i, &xs) == ERROR_SUCCESS) {
                    InterlockedExchange(&g_xiSlot, (LONG)i);
                    shim_log("pad: Xbox controller on XInput user %lu", i);
                    break;
                }
            }
        }
        if (g_xiSlot < 0 && g_ds4 == INVALID_HANDLE_VALUE) open_ds4();
        Sleep(1500);
    }
}

// Under g_cs. XInput first (an Xbox pad, or a PS pad that Steam Input presents as one),
// then the raw-HID DS4.
static void read_pad(PadState* p)
{
    memset(p, 0, sizeof *p);
    LONG slot = g_xiSlot;
    if (slot >= 0 && pXInputGetState) {
        XINPUT_STATE_ xs;
        if (pXInputGetState((DWORD)slot, &xs) == ERROR_SUCCESS) {
            const XINPUT_GAMEPAD_& g = xs.Gamepad;
            p->steer = scale_stick(g.sThumbLX, 6000, 32767);
            p->rx    = scale_stick(g.sThumbRX, 6000, 32767);
            p->ry    = scale_stick(g.sThumbRY, 6000, 32767);
            p->gas   = scale_trigger(g.bRightTrigger);
            p->brake = scale_trigger(g.bLeftTrigger);
            WORD w = g.wButtons; unsigned ub = 0;
            if (w & 0x1000) ub |= UB_CROSS;    if (w & 0x2000) ub |= UB_CIRCLE;
            if (w & 0x4000) ub |= UB_SQUARE;   if (w & 0x8000) ub |= UB_TRIANGLE;
            if (w & 0x0100) ub |= UB_L1;       if (w & 0x0200) ub |= UB_R1;
            if (w & 0x0020) ub |= UB_SHARE;    if (w & 0x0010) ub |= UB_OPTIONS;
            if (w & 0x0040) ub |= UB_L3;       if (w & 0x0080) ub |= UB_R3;
            if (w & 0x0001) ub |= UB_DUP;      if (w & 0x0002) ub |= UB_DDOWN;
            if (w & 0x0004) ub |= UB_DLEFT;    if (w & 0x0008) ub |= UB_DRIGHT;
            if (g.bLeftTrigger  > 30) ub |= UB_L2;
            if (g.bRightTrigger > 30) ub |= UB_R2;
            p->btn = ub;
            p->present = true;
            return;
        }
        InterlockedExchange(&g_xiSlot, -1);    // unplugged: the discovery thread looks again
        shim_log("pad: Xbox controller disconnected");
    }
    if (g_ds4 != INVALID_HANDLE_VALUE) {
        pump_ds4();
        if (g_ds4 != INVALID_HANDLE_VALUE) {
            p->steer = g_ds4Steer; p->gas = g_ds4Gas; p->brake = g_ds4Brake;
            p->rx = g_ds4Rx; p->ry = g_ds4Ry; p->btn = g_ds4Btn;
            p->present = true;
        }
    }
}

// ------------------------------------------------------------------ the button map
// Every action is a cabinet key from Game\config\GvrIO.xml, except the shifter and quit.
enum { S_KEY, S_LEFT, S_RIGHT, S_SHIFTUP, S_SHIFTDOWN, S_QUIT, S_NONE, S_NFSU };
struct ActDef { const char* name; char key; int spec; };

// In a race (NASCAR_GVR.exe). What each cabinet button does there (NASCAR_FINDINGS 14.2):
//   View = cycle the driving cameras, LookBack = rear view while held, Start = put the car
//   back on the track (GVRRestartRunning, with a cool-down), Music = next song.
//   Horn = the cabinet's NOS input (GvrIO.xml key 'j', added by the install): the race
//   plays its looping horn sample on it; GvrIOShim.cpp patch_horn_stop stops it on release.
//   (The engine's .CTL "Control - Horn" makes no sound in this build.)
static const ActDef RACE_ACTS[] = {
    { "view",     'v', S_KEY }, { "camera",   'v', S_KEY },
    { "lookback", 'l', S_KEY },
    { "start",    's', S_KEY }, { "reset",    's', S_KEY },
    { "music",    'm', S_KEY },
    { "horn",     'j', S_KEY }, { "nitrous", 'j', S_KEY },   // GvrIO.xml "NOS" key
    { "shiftup",   0,  S_SHIFTUP }, { "shiftdown", 0, S_SHIFTDOWN },
    { "quit",      0,  S_QUIT },    { "abort",     0, S_QUIT },
    { "none",      0,  S_NONE },
    // NFSU actions with nothing to do in NASCAR - accepted so a copied NFSU map loads
    { "ebrake",    0,  S_NFSU }, { "confirm", 0, S_NFSU }, { "skipintro", 0, S_NFSU },
};
// In the menus (AMPlayer.exe): the operator menu treats View/Music/LookBack as
// up/down/back (on-screen legend), Start selects everywhere.
static const ActDef FRONT_ACTS[] = {
    { "select",   's', S_KEY }, { "start",    's', S_KEY },
    { "back",     'l', S_KEY }, { "lookback", 'l', S_KEY }, { "exit", 'l', S_KEY },
    { "operator", 'o', S_KEY },
    { "up",       'v', S_KEY }, { "view",     'v', S_KEY }, { "navup",   'v', S_KEY },
    { "down",     'm', S_KEY }, { "music",    'm', S_KEY }, { "navdown", 'm', S_KEY },
    { "left",     'd', S_LEFT },  { "navleft",  'd', S_LEFT },
    { "right",    'g', S_RIGHT }, { "navright", 'g', S_RIGHT },
    { "none",      0,  S_NONE },
    { "accept",    0,  S_NFSU }, { "backspace", 0, S_NFSU }, { "card", 0, S_NFSU },
};

static const struct { const char* name; unsigned ub; } PAD_BUTTONS[] = {
    {"cross",UB_CROSS},{"circle",UB_CIRCLE},{"square",UB_SQUARE},{"triangle",UB_TRIANGLE},
    {"l1",UB_L1},{"r1",UB_R1},{"l2",UB_L2},{"r2",UB_R2},{"options",UB_OPTIONS},{"share",UB_SHARE},
    {"l3",UB_L3},{"r3",UB_R3},{"dup",UB_DUP},{"ddown",UB_DDOWN},{"dleft",UB_DLEFT},
    {"dright",UB_DRIGHT},{"ps",UB_PS},
    // Xbox spellings of the same POSITIONS
    {"a",UB_CROSS},{"b",UB_CIRCLE},{"x",UB_SQUARE},{"y",UB_TRIANGLE},
    {"lb",UB_L1},{"rb",UB_R1},{"lt",UB_L2},{"rt",UB_R2},
    {"start",UB_OPTIONS},{"menu",UB_OPTIONS},{"back",UB_SHARE},{"view",UB_SHARE},
    {"ls",UB_L3},{"rs",UB_R3},{"guide",UB_PS},
};

struct Bind { unsigned ub; const ActDef* act; };
struct PadMap {
    const ActDef* acts; int nActs; const char* section;
    Bind b[32]; int n;
};
static PadMap g_race  = { RACE_ACTS,  sizeof(RACE_ACTS)  / sizeof(ActDef), "Controller" };
static PadMap g_front = { FRONT_ACTS, sizeof(FRONT_ACTS) / sizeof(ActDef), "Frontend" };

static const ActDef* find_act(const PadMap& m, const char* name)
{
    for (int i = 0; i < m.nActs; ++i) if (_stricmp(m.acts[i].name, name) == 0) return &m.acts[i];
    return NULL;
}
static void map_clear(PadMap& m, unsigned ub)
{
    int w = 0;
    for (int i = 0; i < m.n; ++i) if (m.b[i].ub != ub) m.b[w++] = m.b[i];
    m.n = w;
}
static void map_add(PadMap& m, unsigned ub, const char* action)
{
    const ActDef* a = find_act(m, action);
    if (!a) { shim_log("pad cfg: [%s] unknown action '%s' - ignored", m.section, action); return; }
    if (a->spec == S_NONE) return;
    if (a->spec == S_NFSU) { shim_log("pad cfg: [%s] '%s' has no NASCAR equivalent - ignored", m.section, action); return; }
    if (m.n < 32) { m.b[m.n].ub = ub; m.b[m.n].act = a; ++m.n; }
}

static unsigned button_of(const char* name)
{
    for (size_t i = 0; i < sizeof(PAD_BUTTONS) / sizeof(PAD_BUTTONS[0]); ++i)
        if (_stricmp(PAD_BUTTONS[i].name, name) == 0) return PAD_BUTTONS[i].ub;
    return 0;
}

static void map_defaults(void)
{
    // Race: NFSU's [Controller] positions. Circle was nitrous there; here it is the horn.
    static const struct { unsigned ub; const char* act; } race[] = {
        { UB_L1, "lookback" }, { UB_R1, "view" }, { UB_OPTIONS, "start" }, { UB_CIRCLE, "horn" },
        { UB_SQUARE, "shiftup" }, { UB_TRIANGLE, "shiftdown" },
        { UB_DRIGHT, "music" }, { UB_DDOWN, "quit" },
    };
    // Menus: NFSU's [Frontend] positions.
    static const struct { unsigned ub; const char* act; } front[] = {
        { UB_CROSS, "select" }, { UB_CIRCLE, "back" }, { UB_OPTIONS, "operator" },
        { UB_DUP, "up" }, { UB_DDOWN, "down" }, { UB_DLEFT, "left" }, { UB_DRIGHT, "right" },
    };
    g_race.n = 0; g_front.n = 0;
    for (size_t i = 0; i < sizeof(race) / sizeof(race[0]); ++i)   map_add(g_race,  race[i].ub,  race[i].act);
    for (size_t i = 0; i < sizeof(front) / sizeof(front[0]); ++i) map_add(g_front, front[i].ub, front[i].act);
}

// nascar_settings.ini sits at the install root; this DLL is in Game\ or Shell\bin\.
bool shim_settings_path(char* out, size_t n)
{
    char dir[MAX_PATH] = { 0 };
    GetModuleFileNameA(g_self, dir, MAX_PATH);
    char* s = strrchr(dir, '\\');
    if (s) *s = 0;
    for (int up = 0; up <= 3; ++up) {
        _snprintf(out, n, "%s\\nascar_settings.ini", dir);
        out[n - 1] = 0;
        if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
        s = strrchr(dir, '\\');
        if (!s) break;
        *s = 0;
    }
    return false;
}

static char* trim(char* p)
{
    while (*p == ' ' || *p == '\t') ++p;
    char* e = p + strlen(p);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return p;
}

// `Button = action[, action...]` lines in [Controller] and [Frontend]. A listed button
// replaces its default; `= none` unbinds it; trailing ; or # comments are allowed.
static void load_config(void)
{
    map_defaults();
    char path[MAX_PATH];
    if (!shim_settings_path(path, sizeof path)) { shim_log("pad cfg: nascar_settings.ini not found - default map"); return; }
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256], section[64] = { 0 };
    int applied = 0;
    while (fgets(line, sizeof line, f)) {
        char* p = trim(line);
        if (!*p || *p == ';' || *p == '#') continue;
        if (*p == '[') {
            char* e = strchr(p, ']');
            if (e) { *e = 0; lstrcpynA(section, p + 1, sizeof section); }
            continue;
        }
        PadMap* m = _stricmp(section, "Controller") == 0 ? &g_race
                  : _stricmp(section, "Frontend")   == 0 ? &g_front : NULL;
        if (!m) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char* btn = trim(p);
        char* act = eq + 1;
        char* cmt = strpbrk(act, ";#");
        if (cmt) *cmt = 0;
        unsigned ub = button_of(btn);
        if (!ub) { shim_log("pad cfg: [%s] unknown button '%s' - ignored", m->section, btn); continue; }
        map_clear(*m, ub);
        for (char* tok = strtok(act, ","); tok; tok = strtok(NULL, ",")) {
            tok = trim(tok);
            if (*tok) map_add(*m, ub, tok);
        }
        ++applied;
    }
    fclose(f);
    shim_log("pad cfg: %s -> %d line(s) applied", path, applied);
}

static void log_map(const PadMap& m)
{
    char buf[512]; buf[0] = 0;
    for (int i = 0; i < m.n; ++i) {
        const char* bn = "?";
        for (size_t k = 0; k < sizeof(PAD_BUTTONS) / sizeof(PAD_BUTTONS[0]); ++k)
            if (PAD_BUTTONS[k].ub == m.b[i].ub) { bn = PAD_BUTTONS[k].name; break; }
        size_t len = strlen(buf);
        _snprintf(buf + len, sizeof buf - len, "%s=%s ", bn, m.b[i].act->name);
        buf[sizeof buf - 1] = 0;
    }
    shim_log("pad map [%s]: %s", m.section, buf);
}

static void pad_start(void)
{
    if (InterlockedCompareExchange(&g_started, 1, 0) != 0) return;
    load_config();
    log_map(g_isShell ? g_front : g_race);
    load_xinput();
    g_ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, discovery_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    InterlockedExchange(&g_ready, 1);
}

static PadState pad_get(void)
{
    PadState p; memset(&p, 0, sizeof p);
    if (g_off) return p;
    pad_start();
    if (!g_ready) return p;
    EnterCriticalSection(&g_cs);
    read_pad(&g_pad);
    p = g_pad;
    LeaveCriticalSection(&g_cs);
    return p;
}

bool pad_process_has_focus(void)
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

// ------------------------------------------------------------------ keys into GvrIO
// The race's sequential gearbox on the cabinet's 4-position H-shifter: gear N is
// "ShifterN held" (GvrIO.xml keys t y u i). Only read in manual transmission; in
// automatic the race ignores it. Every race is a new process, so each starts in 1st.
static int      g_gear = 1;
static unsigned g_prevBtn = 0;
static DWORD    g_quitSince = 0;                 // when the quit button went down; 0 = up
static const DWORD kQuitHoldMs = 1000;           // hold to quit: the abort has no "are you sure"
static char     g_lastKeys[48] = "";

// Holds key `c` in GvrIO's keyboard state: a cabinet letter, or '<' / '>' for VK_LEFT /
// VK_RIGHT. GvrIO turns scan codes back into virtual keys with MapVirtualKeyA, so the scan
// code comes from the reverse call. (The real arrow keys' DirectInput codes 0xCB/0xCD map
// to no virtual key at all there; 0x4B/0x4D - the numpad 4/6 position - give VK_LEFT/RIGHT.)
static void press(BYTE* st, char c)
{
    UINT vk = c == '<' ? VK_LEFT : c == '>' ? VK_RIGHT : (UINT)toupper((unsigned char)c);
    UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    if (sc > 0 && sc < 256) st[sc] |= 0x80;
}

static void inject_keys(BYTE* st)
{
    PadState p = pad_get();
    if (!p.present || !pad_process_has_focus()) { g_prevBtn = 0; g_quitSince = 0; return; }
    unsigned b = p.btn, edge = b & ~g_prevBtn;
    g_prevBtn = b;

    const PadMap& m = g_isShell ? g_front : g_race;
    char keys[48]; int nk = 0;                   // up to 36 mapped + stick arrow + gear + 4 for quit
    bool quitHeld = false;
    for (int i = 0; i < m.n; ++i) {
        if (!(b & m.b[i].ub)) continue;
        const ActDef* a = m.b[i].act;
        switch (a->spec) {
        case S_KEY:
            if (nk < 36) keys[nk++] = a->key;
            break;
        case S_LEFT: case S_RIGHT:              // the cabinet key, plus GvrIO's selection arrow
            if (nk < 35) { keys[nk++] = a->key; keys[nk++] = a->spec == S_LEFT ? '<' : '>'; }
            break;
        case S_SHIFTUP:
            if ((edge & m.b[i].ub) && g_gear < 4) { ++g_gear; shim_log("pad: shift up -> %d", g_gear); }
            break;
        case S_SHIFTDOWN:
            if ((edge & m.b[i].ub) && g_gear > 1) { --g_gear; shim_log("pad: shift down -> %d", g_gear); }
            break;
        case S_QUIT:
            quitHeld = true;
            break;
        }
    }
    if (g_isShell) {
        // GvrIO's own selection (Gvr.SelectionX + onSelectionChanged: transmission select)
        // moves one step per PRESS of VK_LEFT / VK_RIGHT; on the cabinet the wheel drives it.
        // So the stick held over half-way presses the arrow, like turning the wheel.
        if (p.steer <= -64)     keys[nk++] = '<';
        else if (p.steer >= 64) keys[nk++] = '>';
    } else {
        keys[nk++] = "tyui"[g_gear - 1];
        // Quit = the cabinet's own four-button abort (View + LookBack + Start + Music held
        // together, enabled by HKLM\...\GlobalVR\NASCAR FourButtonAbort=1): the race logs
        // "Player initiated ABORT!" and ends through its normal path.
        DWORD now = GetTickCount();
        if (!quitHeld) g_quitSince = 0;
        else if (!g_quitSince) g_quitSince = now ? now : 1;
        else if (now - g_quitSince >= kQuitHoldMs) {
            keys[nk++] = 'v'; keys[nk++] = 'l'; keys[nk++] = 's'; keys[nk++] = 'm';
        }
    }
    keys[nk] = 0;
    for (int i = 0; i < nk; ++i) press(st, keys[i]);
    if (strcmp(keys, g_lastKeys) != 0) {
        lstrcpynA(g_lastKeys, keys, sizeof g_lastKeys);
        shim_log("pad: cabinet keys held '%s'", keys);
    }
}

// Race only: the right stick holds the numpad keys that turn the Swingman (chase) camera
// on the race's OWN DirectInput keyboard (raw scan codes - the engine reads them itself):
// 4 / 6 left / right, 8 / 2 up / down, while the stick is over half-way.
static char g_lastRaceKeys[16] = "";

static void inject_race_keys(BYTE* st)
{
    PadState p = pad_get();
    if (!p.present || !pad_process_has_focus()) return;
    char shown[16]; int ns = 0;
    if (p.rx <= -64)      { st[0x4B] |= 0x80; shown[ns++] = '4'; }
    else if (p.rx >= 64)  { st[0x4D] |= 0x80; shown[ns++] = '6'; }
    if (p.ry >= 64)       { st[0x48] |= 0x80; shown[ns++] = '8'; }
    else if (p.ry <= -64) { st[0x50] |= 0x80; shown[ns++] = '2'; }
    shown[ns] = 0;
    if (strcmp(shown, g_lastRaceKeys) != 0) {
        lstrcpynA(g_lastRaceKeys, shown, sizeof g_lastRaceKeys);
        shim_log("pad: race keys held '%s'", shown);
    }
}

// ------------------------------------------------------------------ DirectInput hooks
static const GUID kIID_IDirectInput8A = { 0xBF798030, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } };
static const GUID kIID_IDirectInput8W = { 0xBF798031, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } };
static const GUID kGUID_SysKeyboard   = { 0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

typedef HRESULT (WINAPI *PFN_DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, void* outer);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateDevice)(void* self, REFGUID guid, void** dev, void* outer);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetDeviceState)(void* self, DWORD cb, LPVOID data);

static PFN_DI8Create      g_realDI8Create      = NULL;
static PFN_CreateDevice   g_realCreateDevice   = NULL;
static PFN_GetDeviceState g_realGetDeviceState = NULL;

// The objects must keep DirectInput's own vtable POINTER: dinput8 validates `this` by
// comparing it against its vtables, and with a swapped pointer CreateDevice failed - GvrIO
// then had no keyboard at all (its physical keys died too; seen live, keyboard ptr 0). So
// the two slots are redirected in DirectInput's shared vtables instead (IDirectInput8:
// CreateDevice = 3, IDirectInputDevice8: GetDeviceState = 9), and the hooks act only for
// the objects GvrIO itself created. Every other object - the race's own keyboard with its
// .CTL keys among them - passes straight through.
static void* volatile g_gvrioDI[4];
static void* volatile g_gvrioKbd[4];

static bool in_set(void* volatile* set, void* p)
{
    for (int i = 0; i < 4; ++i) if (set[i] == p) return true;
    return false;
}
static void add_to_set(void* volatile* set, void* p)
{
    if (in_set(set, p)) return;
    for (int i = 0; i < 4; ++i) if (!set[i]) { set[i] = p; return; }
    set[3] = p;
}
static bool patch_vtable_slot(void** vt, int slot, void* hook, void** orig)
{
    DWORD old = 0;
    if (!VirtualProtect(&vt[slot], sizeof(void*), PAGE_READWRITE, &old)) return false;
    *orig = vt[slot];
    InterlockedExchangePointer(&vt[slot], hook);
    VirtualProtect(&vt[slot], sizeof(void*), old, &old);
    return true;
}

static HRESULT STDMETHODCALLTYPE hook_GetDeviceState(void* self, DWORD cb, LPVOID data)
{
    HRESULT hr = g_realGetDeviceState(self, cb, data);
    // A failed read (window not in front, keyboard not acquired) stays untouched.
    if (SUCCEEDED(hr) && data && cb >= 256) {
        if (in_set(g_gvrioKbd, self)) inject_keys((BYTE*)data);     // GvrIO: cabinet keys
        else if (!g_isShell)          inject_race_keys((BYTE*)data); // the race's own keyboard
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_CreateDevice(void* self, REFGUID guid, void** dev, void* outer)
{
    HRESULT hr = g_realCreateDevice(self, guid, dev, outer);
    if (SUCCEEDED(hr) && dev && *dev && in_set(g_gvrioDI, self) && IsEqualGUID(guid, kGUID_SysKeyboard)) {
        add_to_set(g_gvrioKbd, *dev);
        if (!g_realGetDeviceState &&
            !patch_vtable_slot(*(void***)*dev, 9, (void*)hook_GetDeviceState, (void**)&g_realGetDeviceState))
            shim_log("pad: could not redirect GetDeviceState - pad buttons disabled");
        shim_log("pad: GvrIO keyboard %p hooked - pad buttons act as cabinet keys", *dev);
    }
    return hr;
}

static HRESULT WINAPI hook_DirectInput8Create(HINSTANCE h, DWORD ver, REFIID riid, LPVOID* out, void* outer)
{
    HRESULT hr = g_realDI8Create(h, ver, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out &&
        (IsEqualIID(riid, kIID_IDirectInput8A) || IsEqualIID(riid, kIID_IDirectInput8W))) {
        add_to_set(g_gvrioDI, *out);
        if (!g_realCreateDevice &&
            !patch_vtable_slot(*(void***)*out, 3, (void*)hook_CreateDevice, (void**)&g_realCreateDevice))
            shim_log("pad: could not redirect CreateDevice - pad buttons disabled");
    }
    return hr;
}

// Point one import of `mod` at `hook`; returns the previous target in *orig.
static bool patch_import(HMODULE mod, const char* dll, const char* fn, void* hook, void** orig)
{
    BYTE* base = (BYTE*)mod;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    void* real = NULL;
    HMODULE target = GetModuleHandleA(dll);
    if (target) real = (void*)GetProcAddress(target, fn);
    for (IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp((const char*)(base + imp->Name), dll) != 0) continue;
        IMAGE_THUNK_DATA* iat   = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        IMAGE_THUNK_DATA* names = imp->OriginalFirstThunk ? (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk) : NULL;
        for (int i = 0; iat[i].u1.Function; ++i) {
            bool match = false;
            if (names && !IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal))
                match = strcmp((const char*)((IMAGE_IMPORT_BY_NAME*)(base + names[i].u1.AddressOfData))->Name, fn) == 0;
            else if (real)
                match = (void*)iat[i].u1.Function == real;
            if (!match) continue;
            DWORD old = 0;
            if (!VirtualProtect(&iat[i].u1.Function, sizeof(void*), PAGE_READWRITE, &old)) return false;
            *orig = (void*)iat[i].u1.Function;
            iat[i].u1.Function = (ULONG_PTR)hook;
            VirtualProtect(&iat[i].u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ public
void pad_attach(HMODULE self, bool isShell)
{
    g_self = self;
    g_isShell = isShell;
    g_off = GetEnvironmentVariableA("GVRIOSHIM_NO_PAD", NULL, 0) > 0;
    InitializeCriticalSection(&g_cs);
}

void pad_hook_gvrio(HMODULE oem)
{
    static bool done = false;
    if (g_off || done || !oem) return;
    done = true;
    if (patch_import(oem, "DINPUT8.dll", "DirectInput8Create", (void*)hook_DirectInput8Create, (void**)&g_realDI8Create))
        shim_log("pad: GvrIO_oem.dll DirectInput8Create redirected");
    else
        shim_log("pad: could not find GvrIO_oem.dll's DirectInput8Create import - pad buttons disabled");
}

bool pad_axis(int axis, int* value)
{
    if (g_off || axis < 0 || axis > 2) return false;
    PadState p = pad_get();
    if (!p.present) return false;
    int v = 0;
    if (pad_process_has_focus()) {          // in the background the pad is neutral
        if (axis == 0) {
            v = p.steer;
            // Menus: D-pad left/right steer like the keyboard's Left/Right do, so car and
            // track select work without the stick.
            if (g_isShell && v == 0)
                for (int i = 0; i < g_front.n; ++i)
                    if (p.btn & g_front.b[i].ub) {
                        if (g_front.b[i].act->spec == S_LEFT)  v = -60;
                        if (g_front.b[i].act->spec == S_RIGHT) v = 60;
                    }
        }
        else if (axis == 1) v = p.gas;
        else                v = p.brake;
    }
    *value = v;
    return true;
}
