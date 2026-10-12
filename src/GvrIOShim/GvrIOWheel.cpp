// ============================================================================
//  GvrIOWheel.cpp  -  PC steering wheels (Logitech Driving Force GT and any
//                     DirectInput driving device) for NASCAR, with force feedback
//                     (compiled into the GvrIO.dll shim next to GvrIOPad.cpp)
//
//  The cabinet's wheel and pedals reach both programs as GvrIO msg 5 axes, and
//  its buttons as GvrIO.xml cabinet keys (see GvrIOPad.cpp). A PC wheel plugs in
//  at exactly the same place: wheel_read() fills the same PadState the pads do -
//  steering / gas / brake, and the wheel's buttons as pad POSITIONS - so the
//  [Controller] / [Frontend] maps apply to a wheel unchanged.
//
//  Device: the first attached DirectInput game controller whose type is
//  DI8DEVTYPE_DRIVING (or whose name contains [Wheel] Device=, if set). It is
//  read with our OWN IDirectInput8 - which shares DirectInput's vtables with
//  GvrIO's, so GvrIOPad.cpp's GetDeviceState hook must skip it (wheel_owns).
//
//  Pedals (Gas / Brake / Pedals = auto, the default): the wheel's USB vendor:product
//  ID picks a preset (g_presets) - which axis is gas and which is brake, and whether
//  both pedals share ONE axis (a Logitech wheel without Logitech G HUB / Gaming
//  Software is 046d:c294, pedals combined on Y). An unknown wheel gets gas = Rz,
//  brake = Y (Logitech's Windows order), and a pedal axis that rests in the MIDDLE
//  is taken as combined. Each pedal's released position is learned from the
//  wheel's first real report - DirectInput shows every axis at its middle until
//  then - and full = the far end of the axis from there. SwapPedals swaps gas and
//  brake on any layout.
//
//  Force feedback - [Wheel] ForceFeedback:
//    arcade (default)  the OEM GvrIO plug-in Steering.dll does it, exactly as on the
//                      cabinet: it opens any attached force-feedback DI8DEVTYPE_DRIVING
//                      device itself (exclusive, background) and runs the race's
//                      effects - centring spring, damper, grass / rumble-strip sines,
//                      collision jolts (GvrIO device 5 msgs 0x1d / 0x35 / 0x19 / 0x1b;
//                      NASCAR_CONTROLS.md 4.y). Steering.dll reads only the wheel's X
//                      axis - on the cabinet the pedals come from the I/O board - so the
//                      steering and pedal VALUES still come from here. We read the wheel
//                      NON-exclusively, which never conflicts with its exclusive hold.
//    shim              our own centring spring + damper (arcade "Normal" strengths, 40 /
//                      40), for a wheel Steering.dll does not pick up. Needs exclusive
//                      access tied to one of our windows: the root of our own foreground
//                      window (DISCL_EXCLUSIVE | DISCL_FOREGROUND), so the shell and the
//                      race hand it over as focus moves.
//    off               none of ours. Steering.dll still runs the arcade effects if it
//                      finds the wheel; turn the strength down in the wheel's driver
//                      (Logitech G HUB) to remove those.
//
//  Settings: nascar_settings.ini [Wheel]. GVRIOSHIM_NO_WHEEL=1 turns it off.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "GvrIOPad.h"
#include "GvrIOWheel.h"

static CRITICAL_SECTION g_wcs;               // everything below; never take GvrIOPad's g_cs inside it
static bool g_woff = false;

typedef HRESULT (WINAPI *PFN_DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static LPDIRECTINPUT8A       g_di   = NULL;
static LPDIRECTINPUTDEVICE8A g_dev  = NULL;   // the wheel
static char  g_name[MAX_PATH] = "";
static bool  g_ffb = false;                   // the device reported force feedback
static HWND  g_coopWnd = NULL;                // window the cooperative level is set for
static bool  g_acquired = false;

// ---------------------------------------------------------------- settings ([Wheel])
enum { AX_X, AX_Y, AX_Z, AX_RX, AX_RY, AX_RZ, AX_S0, AX_S1, AX_NONE, AX_AUTO = -2 };
enum { PD_AUTO, PD_SEPARATE, PD_COMBINED };
static int   g_axSteer = AX_X, g_axGas = AX_AUTO, g_axBrake = AX_AUTO;   // as set in [Wheel]
static int   g_pedalsCfg = PD_AUTO;
static bool  g_swapPedals = false;
static bool  g_oldDefault = false;            // the v1.0.9 template's Gas = y / Brake = rz, ignored
// the pedal layout in use for the attached wheel ([Wheel] settings, else its preset, else defaults)
static int   g_useGas = AX_RZ, g_useBrake = AX_Y;
static bool  g_combined = false;              // one pedal axis: gas one way, brake the other
static bool  g_gasDown = true;                // combined: gas pulls the axis down
static bool  g_combinedAuto = false;          // may still switch to combined if an axis rests mid
static unsigned g_axPresent = 0;              // bit per AX_ the wheel really has
static int   g_rotation = 0, g_range = 900;   // 0 = use the wheel's own range (no scaling)
static int   g_deadzone = 2;                  // % of half range, steering
static int   g_pedalDead = 4;                 // % of full range, pedals
static unsigned g_btnMap[32];                 // DirectInput button -> pad position bit
static bool  g_btnUser[32];                   // set in [Wheel] - device defaults leave it alone
static char  g_devFilter[64] = "";
// force feedback (percent)
// shim force feedback (percent); defaults = the arcade's "Normal" Steering Feedback Strength
static int   g_ffbGain = 100, g_ffbSpring = 40, g_ffbDamper = 40, g_ffbForce = 100;
static bool  g_ffbOn = false;                 // true only for ForceFeedback = shim

static const char* s_axName[9] = { "x", "y", "z", "rx", "ry", "rz", "slider0", "slider1", "none" };

static int axis_of(const char* key, const char* s, int def)
{
    for (int i = 0; i < 9; ++i) if (_stricmp(s, s_axName[i]) == 0) return i;
    if (_stricmp(s, "slider") == 0) return AX_S0;
    if (_stricmp(s, "auto") == 0 && def == AX_AUTO) return AX_AUTO;
    shim_log("wheel cfg: %s = '%s' is not an axis (x y z rx ry rz slider0 slider1%s) - ignored",
             key, s, def == AX_AUTO ? " auto" : "");
    return def;
}

static const char* ax_name(int ax) { return ax >= 0 && ax < 9 ? s_axName[ax] : "auto"; }

static void load_wheel_config(void)
{
    // defaults: Logitech Driving Force GT in native mode (Logitech G HUB / Gaming Software
    // installed): 0 Cross 1 Square 2 Circle 3 Triangle 4 right paddle 5 left paddle 6 R2 7 L2
    // 8 Select 9 Start 10 R3 11 L3. Paddles shift, like the cabinet's shifter.
    static const char* defs[32] = {
        "cross", "r1", "circle", "l1", "square", "triangle", "r2", "l2",
        "share", "options", "r3", "l3", 0, 0, 0, 0, 0, 0, 0, "ps",
    };
    for (int i = 0; i < 32; ++i) g_btnMap[i] = defs[i] ? pad_button_bit(defs[i]) : 0;

    char ini[MAX_PATH];
    if (!shim_settings_path(ini, sizeof ini)) return;
    char v[64];
    #define S(key, def) (GetPrivateProfileStringA("Wheel", key, def, v, sizeof v, ini), v)
    if (_stricmp(S("Enabled", "true"), "false") == 0 || strcmp(v, "0") == 0) g_woff = true;
    lstrcpynA(g_devFilter, S("Device", ""), sizeof g_devFilter);
    g_axSteer = axis_of("Steering", S("Steering", "x"), AX_X);
    g_axGas   = axis_of("Gas", S("Gas", "auto"), AX_AUTO);
    g_axBrake = axis_of("Brake", S("Brake", "auto"), AX_AUTO);
    S("Pedals", "auto");
    g_pedalsCfg = _stricmp(v, "combined") == 0 ? PD_COMBINED : _stricmp(v, "separate") == 0 ? PD_SEPARATE : PD_AUTO;
    if (g_pedalsCfg == PD_AUTO && _stricmp(v, "auto") != 0)
        shim_log("wheel cfg: Pedals = '%s' - use auto, separate or combined", v);
    // Up to v1.0.9 every nascar_settings.ini got Gas = y / Brake = rz / Pedals = separate - the
    // old default, backwards for Logitech wheels on Windows - and updates keep the player's
    // values. That exact set is not a choice anyone made: treat it as auto.
    if (g_axGas == AX_Y && g_axBrake == AX_RZ && g_pedalsCfg != PD_COMBINED) {
        g_axGas = g_axBrake = AX_AUTO; g_pedalsCfg = PD_AUTO; g_oldDefault = true;
    }
    S("SwapPedals", "false");
    g_swapPedals = _stricmp(v, "true") == 0 || strcmp(v, "1") == 0;
    g_rotation = atoi(S("Rotation", "0"));
    g_range    = atoi(S("DeviceRange", "900"));
    g_deadzone = atoi(S("SteeringDeadZone", "2"));
    g_pedalDead = atoi(S("PedalDeadZone", "4"));
    S("ForceFeedback", "arcade");
    g_ffbOn     = _stricmp(v, "shim") == 0;
    if (!g_ffbOn && _stricmp(v, "arcade") != 0 && _stricmp(v, "off") != 0)
        shim_log("wheel cfg: ForceFeedback = '%s' - use arcade, shim or off", v);
    g_ffbGain   = atoi(S("ForceGain", "100"));
    g_ffbSpring = atoi(S("ForceSpring", "40"));
    g_ffbDamper = atoi(S("ForceDamper", "40"));
    g_ffbForce  = atoi(S("ForceRoad", "100"));
    for (int i = 0; i < 32; ++i) {                // Button0 = cross ... (position names)
        char key[16]; _snprintf(key, sizeof key, "Button%d", i); key[15] = 0;
        GetPrivateProfileStringA("Wheel", key, "", v, sizeof v, ini);
        if (!v[0]) continue;
        g_btnUser[i] = true;
        g_btnMap[i] = _stricmp(v, "none") == 0 ? 0 : pad_button_bit(v);
        if (!g_btnMap[i] && _stricmp(v, "none") != 0)
            shim_log("wheel cfg: Button%d = '%s' is not a pad button name - ignored", i, v);
    }
    #undef S
    shim_log("wheel cfg: steer=%s gas=%s brake=%s pedals=%s%s%s rotation=%d/%d force feedback=%s (gain=%d spring=%d damper=%d)",
             ax_name(g_axSteer), ax_name(g_axGas), ax_name(g_axBrake),
             g_pedalsCfg == PD_COMBINED ? "combined" : g_pedalsCfg == PD_SEPARATE ? "separate" : "auto",
             g_swapPedals ? " swapped" : "", g_oldDefault ? " (old Gas = y / Brake = rz default ignored)" : "",
             g_rotation, g_range,
             g_ffbOn ? "shim" : "arcade/off - ours not used", g_ffbGain, g_ffbSpring, g_ffbDamper);
}

// ---------------------------------------------------------------- force feedback
static LPDIRECTINPUTEFFECT g_eSpring = NULL, g_eDamper = NULL, g_eConst = NULL;
static DWORD g_axesFfb[1] = { DIJOFS_X };
static LONG  g_dirFfb[1]  = { 0 };
static int   g_forceTarget = 0;               // -10000..10000, from wheel_set_force
static int   g_springTarget = -1;             // 0..100 %, -1 = configured default
static int   g_lastForce = 0x7FFFFFFF, g_lastSpring = -1;
static DWORD g_lastFfbTick = 0;

static int pct(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

static LPDIRECTINPUTEFFECT make_condition(REFGUID g, int coeffPct)
{
    DICONDITION c; memset(&c, 0, sizeof c);
    c.lPositiveCoefficient = c.lNegativeCoefficient = pct(coeffPct) * 100;
    c.dwPositiveSaturation = c.dwNegativeSaturation = 10000;
    DIEFFECT e; memset(&e, 0, sizeof e);
    e.dwSize = sizeof e; e.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.dwDuration = INFINITE; e.dwGain = pct(g_ffbGain) * 100;
    e.dwTriggerButton = DIEB_NOTRIGGER; e.cAxes = 1; e.rgdwAxes = g_axesFfb; e.rglDirection = g_dirFfb;
    e.cbTypeSpecificParams = sizeof c; e.lpvTypeSpecificParams = &c;
    LPDIRECTINPUTEFFECT fx = NULL;
    if (FAILED(g_dev->CreateEffect(g, &e, &fx, NULL))) return NULL;
    fx->Start(1, 0);
    return fx;
}

static void ffb_create(void)
{
    if (!g_ffb || !g_ffbOn || g_eSpring || g_eDamper || g_eConst) return;
    // our own spring replaces the wheel's built-in auto-centre
    DIPROPDWORD ac; ac.diph.dwSize = sizeof ac; ac.diph.dwHeaderSize = sizeof ac.diph;
    ac.diph.dwObj = 0; ac.diph.dwHow = DIPH_DEVICE; ac.dwData = DIPROPAUTOCENTER_OFF;
    g_dev->SetProperty(DIPROP_AUTOCENTER, &ac.diph);
    g_eSpring = make_condition(GUID_Spring, g_ffbSpring);
    g_eDamper = make_condition(GUID_Damper, g_ffbDamper);
    DICONSTANTFORCE cf = { 0 };
    DIEFFECT e; memset(&e, 0, sizeof e);
    e.dwSize = sizeof e; e.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.dwDuration = INFINITE; e.dwGain = pct(g_ffbGain) * 100;
    e.dwTriggerButton = DIEB_NOTRIGGER; e.cAxes = 1; e.rgdwAxes = g_axesFfb; e.rglDirection = g_dirFfb;
    e.cbTypeSpecificParams = sizeof cf; e.lpvTypeSpecificParams = &cf;
    if (SUCCEEDED(g_dev->CreateEffect(GUID_ConstantForce, &e, &g_eConst, NULL))) g_eConst->Start(1, 0);
    g_lastForce = 0x7FFFFFFF; g_lastSpring = -1;
    shim_log("wheel: force feedback on (spring %s, damper %s, constant force %s)",
             g_eSpring ? "ok" : "FAILED", g_eDamper ? "ok" : "FAILED", g_eConst ? "ok" : "FAILED");
}

static void ffb_release(void)
{
    LPDIRECTINPUTEFFECT* all[3] = { &g_eSpring, &g_eDamper, &g_eConst };
    for (int i = 0; i < 3; ++i) if (*all[i]) { (*all[i])->Stop(); (*all[i])->Release(); *all[i] = NULL; }
}

// Under g_wcs, from wheel_read: push the latest targets to the wheel, at most every 10 ms.
static void ffb_update(void)
{
    if (!g_eConst && !g_eSpring) return;
    DWORD now = GetTickCount();
    if (now - g_lastFfbTick < 10) return;
    g_lastFfbTick = now;
    int force = g_forceTarget * pct(g_ffbForce) / 100;
    if (g_eConst && force != g_lastForce) {
        DICONSTANTFORCE cf; cf.lMagnitude = force;
        DIEFFECT e; memset(&e, 0, sizeof e);
        e.dwSize = sizeof e; e.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        e.cbTypeSpecificParams = sizeof cf; e.lpvTypeSpecificParams = &cf;
        if (SUCCEEDED(g_eConst->SetParameters(&e, DIEP_TYPESPECIFICPARAMS))) g_lastForce = force;
    }
    int spring = g_springTarget < 0 ? g_ffbSpring : g_springTarget;
    if (g_eSpring && spring != g_lastSpring) {
        DICONDITION c; memset(&c, 0, sizeof c);
        c.lPositiveCoefficient = c.lNegativeCoefficient = pct(spring) * 100;
        c.dwPositiveSaturation = c.dwNegativeSaturation = 10000;
        DIEFFECT e; memset(&e, 0, sizeof e);
        e.dwSize = sizeof e; e.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        e.cbTypeSpecificParams = sizeof c; e.lpvTypeSpecificParams = &c;
        if (SUCCEEDED(g_eSpring->SetParameters(&e, DIEP_TYPESPECIFICPARAMS))) g_lastSpring = spring;
    }
}

void wheel_set_force(int force, int springPct)
{
    if (g_woff) return;
    if (force > 10000) force = 10000;
    if (force < -10000) force = -10000;
    EnterCriticalSection(&g_wcs);
    g_forceTarget = force;
    g_springTarget = springPct;
    LeaveCriticalSection(&g_wcs);
}

// ---------------------------------------------------------------- discovery
struct FindCtx { GUID guid; DWORD product; char name[MAX_PATH]; bool found; bool ffb; };

static BOOL CALLBACK enum_cb(LPCDIDEVICEINSTANCEA d, LPVOID p)
{
    FindCtx* c = (FindCtx*)p;
    BYTE type = GET_DIDEVICE_TYPE(d->dwDevType);
    bool match = g_devFilter[0] ? (strstr(d->tszProductName, g_devFilter) != NULL ||
                                    strstr(d->tszInstanceName, g_devFilter) != NULL)
                                : type == DI8DEVTYPE_DRIVING;
    if (!match) return DIENUM_CONTINUE;
    c->guid = d->guidInstance;
    c->product = d->guidProduct.Data1;            // MAKELONG(vendor, product) for USB devices
    lstrcpynA(c->name, d->tszProductName, sizeof c->name);
    c->found = true;
    return DIENUM_STOP;
}

// Every axis reports 0..65535 over its WHOLE travel: no dead zone, and saturation 100 % - a
// lower saturation (left in Windows' game controller settings) reads full before the pedal
// is down.
static void set_range(DWORD ofs)
{
    DIPROPRANGE r; r.diph.dwSize = sizeof r; r.diph.dwHeaderSize = sizeof r.diph;
    r.diph.dwObj = ofs; r.diph.dwHow = DIPH_BYOFFSET; r.lMin = 0; r.lMax = 65535;
    g_dev->SetProperty(DIPROP_RANGE, &r.diph);
    DIPROPDWORD dz; dz.diph.dwSize = sizeof dz; dz.diph.dwHeaderSize = sizeof dz.diph;
    dz.diph.dwObj = ofs; dz.diph.dwHow = DIPH_BYOFFSET; dz.dwData = 0;
    g_dev->SetProperty(DIPROP_DEADZONE, &dz.diph);
    dz.dwData = 10000;
    g_dev->SetProperty(DIPROP_SATURATION, &dz.diph);
}

// Pedal layouts by USB vendor:product (LOWORD = vendor, HIWORD = product, as DIPROP_VIDPID).
// gear1 = DirectInput button of the H-shifter's 1st gear (-1: none known); 2nd..6th follow.
// sure = false: from Linux driver data, not yet confirmed on Windows (the log says so).
#define VP(v, p) ((DWORD)MAKELONG(0x##v, 0x##p))
struct WheelPreset { DWORD id; const char* name; int gas, brake; bool combined; int gear1; bool sure; };
static const WheelPreset g_presets[] = {
    // Logitech without G HUB / Gaming Software: DFGT, G25, G27 report as this "Driving Force EX",
    // gas and brake on Y, resting in the middle; gas pulls it down.
    { VP(046d, c294), "Logitech wheel in compatibility mode", AX_Y,  AX_Y,  true,  -1, true },
    // Logitech native mode: brake Y, gas Rz, clutch a slider
    { VP(046d, c29a), "Logitech Driving Force GT",            AX_RZ, AX_Y,  false, -1, true },
    { VP(046d, c298), "Logitech Driving Force Pro",           AX_RZ, AX_Y,  false, -1, true },
    { VP(046d, c299), "Logitech G25",                         AX_RZ, AX_Y,  false, -1, true },
    { VP(046d, c29b), "Logitech G27",                         AX_RZ, AX_Y,  false,  8, true },
    { VP(046d, c24f), "Logitech G29",                         AX_RZ, AX_Y,  false, 12, true },
    { VP(046d, c262), "Logitech G920",                        AX_RZ, AX_Y,  false, 13, true },
    { VP(046d, c266), "Logitech G923 (PlayStation)",          AX_RZ, AX_Y,  false, 12, true },
    { VP(046d, c26e), "Logitech G923 (Xbox)",                 AX_RZ, AX_Y,  false, 13, true },
    // Thrustmaster: the layout depends on the wheel's mode (PS3 / PS4 switch)
    { VP(044f, b66e), "Thrustmaster T300RS (PS3 mode)",       AX_Z,  AX_RZ, false, -1, false },
    { VP(044f, b66d), "Thrustmaster T300RS (PS4 mode)",       AX_Y,  AX_Z,  false, -1, false },
    { VP(044f, b664), "Thrustmaster TX",                      AX_Z,  AX_RZ, false, -1, false },
};
#undef VP

// Pedal reading state (see wheel_read); reset for every newly found wheel.
struct PedalState { long rest; unsigned logged; };
static PedalState g_pg, g_pb;                 // gas, brake (combined: g_pg holds the axis' rest)
static bool  g_realReport = false;            // the wheel has sent a report of its own
static long  g_axStart[8]; static bool g_axHave, g_axLogged[8];

static void pedals_reset(void)
{
    g_pg.rest = g_pb.rest = -1; g_pg.logged = g_pb.logged = 0;
    g_realReport = false; g_axHave = false;
    memset(g_axLogged, 0, sizeof g_axLogged);
}

// Under g_wcs: decide the pedal layout for the wheel just found.
static void choose_layout(DWORD id, const char* devName)
{
    const WheelPreset* p = NULL;
    for (size_t i = 0; i < sizeof g_presets / sizeof g_presets[0]; ++i)
        if (g_presets[i].id == id) { p = &g_presets[i]; break; }
    int gas = AX_RZ, brake = AX_Y;
    bool comb = false;
    const char* from = "default - not a known model";
    if (p) { gas = p->gas; brake = p->brake; comb = p->combined; from = p->name; }
    else if ((g_axPresent & (1u << AX_Y)) && !(g_axPresent & ((1u << AX_Z) | (1u << AX_RZ)))) {
        gas = brake = AX_Y; comb = true; from = "Y is its only pedal axis";
    }
    if (g_pedalsCfg != PD_AUTO) {
        bool want = g_pedalsCfg == PD_COMBINED;
        if (want && !comb) gas = AX_Y;               // the usual combined axis
        comb = want;
    }
    if (g_axGas != AX_AUTO) gas = g_axGas;
    if (g_axBrake != AX_AUTO && !comb) brake = g_axBrake;
    if (comb) brake = gas;
    else if (g_swapPedals) { int t = gas; gas = brake; brake = t; }
    g_useGas = gas; g_useBrake = brake; g_combined = comb;
    g_gasDown = !g_swapPedals;
    // an unknown or unconfirmed layout may still turn out to be one combined axis
    g_combinedAuto = g_pedalsCfg == PD_AUTO && !comb && (!p || !p->sure);
    if (comb)
        shim_log("wheel: pedals combined on '%s', gas pulls it %s (%s)", ax_name(gas),
                 g_gasDown ? "down" : "up", from);
    else
        shim_log("wheel: pedals gas = '%s', brake = '%s'%s (%s)%s", ax_name(gas), ax_name(brake),
                 g_swapPedals ? " (swapped)" : "", from,
                 p && !p->sure ? " - layout not yet confirmed on Windows: if gas brakes, set [Wheel] SwapPedals = true" : "");

    // H-shifter gears are buttons: gear 1..6 (the arcade shifter has 4 positions: 5th, 6th = 4th)
    int g1 = p ? p->gear1 : -1;
    if (!p && (strstr(devName, "G29") || strstr(devName, "G923"))) g1 = 12;
    if (!p && strstr(devName, "G920")) g1 = 13;
    if (g1 >= 0) {
        static const char* gears[6] = { "gear1", "gear2", "gear3", "gear4", "gear4", "gear4" };
        for (int i = 0; i < 6; ++i)
            if (!g_btnUser[g1 + i]) g_btnMap[g1 + i] = pad_button_bit(gears[i]);
        shim_log("wheel: H-shifter defaults - buttons %d..%d = gear 1, 2, 3, 4, 4, 4 ([Wheel] ButtonN overrides)",
                 g1, g1 + 5);
    }
}

// Discovery thread, every ~1.5 s while there is no wheel.
void wheel_discover(void)
{
    static bool configured = false;
    if (!configured) {
        configured = true;
        g_woff = GetEnvironmentVariableA("GVRIOSHIM_NO_WHEEL", NULL, 0) > 0;
        if (!g_woff) load_wheel_config();
        if (g_woff) shim_log("wheel: off");
    }
    if (g_woff || g_dev) return;
    if (!g_di) {
        HMODULE h = LoadLibraryA("dinput8.dll");
        PFN_DI8Create create = h ? (PFN_DI8Create)GetProcAddress(h, "DirectInput8Create") : NULL;
        if (!create || FAILED(create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8A, (void**)&g_di, NULL))) {
            shim_log("wheel: DirectInput unavailable - wheel support off");
            g_woff = true;
            return;
        }
    }
    FindCtx c; memset(&c, 0, sizeof c);
    g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, enum_cb, &c, DIEDFL_ATTACHEDONLY);
    if (!c.found) return;

    LPDIRECTINPUTDEVICE8A dev = NULL;
    if (FAILED(g_di->CreateDevice(c.guid, &dev, NULL))) return;
    if (FAILED(dev->SetDataFormat(&c_dfDIJoystick2))) { dev->Release(); return; }
    DIDEVCAPS caps; caps.dwSize = sizeof caps;
    bool ffb = SUCCEEDED(dev->GetCapabilities(&caps)) && (caps.dwFlags & DIDC_FORCEFEEDBACK);
    DIPROPDWORD vp; vp.diph.dwSize = sizeof vp; vp.diph.dwHeaderSize = sizeof vp.diph;
    vp.diph.dwObj = 0; vp.diph.dwHow = DIPH_DEVICE; vp.dwData = 0;
    DWORD id = SUCCEEDED(dev->GetProperty(DIPROP_VIDPID, &vp.diph)) && vp.dwData ? vp.dwData : c.product;

    EnterCriticalSection(&g_wcs);
    g_dev = dev; g_ffb = ffb; g_coopWnd = NULL; g_acquired = false;
    lstrcpynA(g_name, c.name, sizeof g_name);
    static const DWORD ofs[8] = { DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ,
                                  DIJOFS_SLIDER(0), DIJOFS_SLIDER(1) };
    char axes[64] = "";
    g_axPresent = 0;
    for (int i = 0; i < 8; ++i) {
        DIDEVICEOBJECTINSTANCEA oi; oi.dwSize = sizeof oi;
        if (SUCCEEDED(dev->GetObjectInfo(&oi, ofs[i], DIPH_BYOFFSET))) {
            g_axPresent |= 1u << i;
            strcat(axes, " "); strcat(axes, s_axName[i]);
        }
        set_range(ofs[i]);
    }
    shim_log("wheel: '%s' found (%04x:%04x, %lu axes:%s, %lu buttons, force feedback %s)", c.name,
             LOWORD(id), HIWORD(id), caps.dwAxes, axes, caps.dwButtons, ffb ? "yes" : "no");
    pedals_reset();
    choose_layout(id, c.name);
    LeaveCriticalSection(&g_wcs);
}

bool wheel_owns(void* device)
{
    return device && device == (void*)g_dev;
}

bool wheel_present(void)
{
    return !g_woff && g_dev != NULL;
}

// ---------------------------------------------------------------- reading
static void drop_device(const char* why)
{
    ffb_release();
    if (g_dev) { g_dev->Unacquire(); g_dev->Release(); g_dev = NULL; }
    g_acquired = false; g_coopWnd = NULL;
    shim_log("wheel: '%s' closed (%s)", g_name, why);
}

// Exclusive foreground access on the root of our own foreground window. Called only while
// this process has focus.
static bool ensure_acquired(void)
{
    HWND fg = GetForegroundWindow();
    HWND root = fg ? GetAncestor(fg, GA_ROOT) : NULL;
    if (!root) return false;
    if (root != g_coopWnd) {
        ffb_release();
        g_dev->Unacquire(); g_acquired = false;
        DWORD level = g_ffb && g_ffbOn ? DISCL_EXCLUSIVE | DISCL_FOREGROUND : DISCL_NONEXCLUSIVE | DISCL_BACKGROUND;
        if (FAILED(g_dev->SetCooperativeLevel(root, level)) &&
            FAILED(g_dev->SetCooperativeLevel(root, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))) return false;
        g_coopWnd = root;
    }
    if (!g_acquired) {
        HRESULT hr = g_dev->Acquire();
        if (FAILED(hr)) return false;
        g_acquired = true;
        ffb_create();
    }
    return true;
}

static long axis_val(const DIJOYSTATE2& s, int ax)
{
    switch (ax) {
    case AX_X: return s.lX;   case AX_Y: return s.lY;   case AX_Z: return s.lZ;
    case AX_RX: return s.lRx; case AX_RY: return s.lRy; case AX_RZ: return s.lRz;
    case AX_S0: return s.rglSlider[0]; case AX_S1: return s.rglSlider[1];
    }
    return -1;
}

// Pedal value 0..255 for `travel` out of the `span` from released to fully down.
static int pedal(long travel, long span)
{
    long dead = span * g_pedalDead / 100;
    if (travel <= dead || span - dead <= 0) return 0;
    long v = (travel - dead) * 255 / (span - dead);
    return v > 255 ? 255 : (int)v;
}

static bool mid_band(long v) { return v > 65535 / 4 && v < 65535 / 4 * 3; }

// What a pedal really does, once: the raw value at 50 %, 90 % and 100 %.
static void pedal_log(PedalState& p, const char* what, int v, int ax, long raw, long rest)
{
    static const int th[3] = { 128, 230, 255 }, pc[3] = { 50, 90, 100 };
    for (int i = 0; i < 3; ++i)
        if (v >= th[i] && !(p.logged & (1u << i))) {
            p.logged |= 1u << i;
            shim_log("wheel: %s at %d%% (axis '%s' = %ld, released = %ld)", what, pc[i], ax_name(ax), raw, rest);
        }
}

// A separate pedal rests at one END of its axis (Logitech: 65535 released, 0 down; others the
// other way round). Its released value is learned from the wheel's first real report and
// follows the pedal if it later reads more released than that (a pedal touched at start-up).
static int separate_pedal(PedalState& p, long raw, int ax, const char* what)
{
    if (raw < 0) return 0;
    if (p.rest < 0) { p.rest = raw; shim_log("wheel: %s pedal released at %ld (axis '%s')", what, raw, ax_name(ax)); }
    long down = p.rest >= 32768 ? 0 : 65535;   // the fully pressed end
    if (labs(raw - down) > labs(p.rest - down)) p.rest = raw;
    int v = pedal(labs(raw - p.rest), labs(down - p.rest));
    pedal_log(p, what, v, ax, raw, p.rest);
    return v;
}

// Before the wheel's first report DirectInput shows every axis at its middle (32767 / 32768),
// which is no pedal's real released position: learn nothing until a real value shows up.
static bool real_report(const DIJOYSTATE2& s)
{
    if (!g_axPresent) return true;
    for (int i = 0; i < 8; ++i)
        if ((g_axPresent & (1u << i)) && labs(axis_val(s, i) - 32767) > 1) return true;
    for (int i = 0; i < 32; ++i) if (s.rgbButtons[i] & 0x80) return true;
    return LOWORD(s.rgdwPOV[0]) != 0xFFFF;
}

// Under g_wcs: the pedals' 0..255 from one reading.
static void read_pedals(const DIJOYSTATE2& s, int* gas, int* brake)
{
    *gas = *brake = 0;
    if (!g_realReport) {
        if (!real_report(s)) return;
        g_realReport = true;
    }
    if (g_combinedAuto) {
        // unknown layout: a pedal axis that rests in the middle is both pedals on one axis
        g_combinedAuto = false;
        long g = axis_val(s, g_useGas), b = axis_val(s, g_useBrake);
        int ax = mid_band(b) ? g_useBrake : mid_band(g) ? g_useGas : -1;
        if (ax >= 0) {
            g_combined = true; g_useGas = g_useBrake = ax;
            shim_log("wheel: axis '%s' rests in the middle (%ld) - pedals combined on it, gas pulls it %s",
                     ax_name(ax), axis_val(s, ax), g_gasDown ? "down" : "up");
        }
    }
    if (g_combined) {
        long y = axis_val(s, g_useGas);
        if (y < 0) return;
        if (g_pg.rest < 0) {
            g_pg.rest = mid_band(y) ? y : 32767;
            shim_log("wheel: combined pedals released at %ld (axis '%s' reads %ld)", g_pg.rest, ax_name(g_useGas), y);
        }
        long r = g_pg.rest;
        bool down = y < r;
        int v = pedal(labs(y - r), down ? r : 65535 - r);
        if (down == g_gasDown) { *gas = v;   pedal_log(g_pg, "gas", v, g_useGas, y, r); }
        else                   { *brake = v; pedal_log(g_pb, "brake", v, g_useGas, y, r); }
    } else {
        *gas   = separate_pedal(g_pg, axis_val(s, g_useGas), g_useGas, "gas");
        *brake = separate_pedal(g_pb, axis_val(s, g_useBrake), g_useBrake, "brake");
    }
}

bool wheel_read(int* steer, int* gas, int* brake, unsigned* buttons)
{
    if (g_woff || !g_dev) return false;
    EnterCriticalSection(&g_wcs);
    bool ok = false;
    if (g_dev) {
        DIJOYSTATE2 s;
        HRESULT hr = DIERR_NOTACQUIRED;
        if (pad_process_has_focus() && ensure_acquired()) {
            g_dev->Poll();
            hr = g_dev->GetDeviceState(sizeof s, &s);
            if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) { g_acquired = false; ffb_release(); }
        }
        if (SUCCEEDED(hr)) {
            read_pedals(s, gas, brake);
            // Which axis is which differs per wheel and driver: log each axis once, the first time
            // it moves well away from where it was at the wheel's first real report (its name is
            // what [Wheel] Gas / Brake take).
            for (int i = 0; g_realReport && i < 8; ++i) {
                long v = axis_val(s, i);
                if (!g_axHave) { g_axStart[i] = v; continue; }
                if (!g_axLogged[i] && labs(v - g_axStart[i]) > 13000) {
                    g_axLogged[i] = true;
                    shim_log("wheel: axis '%s' moves (from %ld to %ld)", s_axName[i], g_axStart[i], v);
                }
            }
            if (g_realReport) g_axHave = true;
            // steering: centre 32768 -> -127..127, optional rotation scaling and dead zone
            double x = (axis_val(s, g_axSteer) - 32768) / 32768.0;
            if (g_rotation > 0 && g_range > g_rotation) x *= (double)g_range / g_rotation;
            double dz = g_deadzone / 100.0, a = fabs(x);
            a = a <= dz ? 0 : (a - dz) / (1 - dz);
            if (a > 1) a = 1;
            *steer = (int)(a * 127 + 0.5) * (x < 0 ? -1 : 1);
            unsigned ub = 0;
            static unsigned prevRaw = 0;
            unsigned raw = 0;
            for (int i = 0; i < 32; ++i) if (s.rgbButtons[i] & 0x80) { raw |= 1u << i; ub |= g_btnMap[i]; }
            for (int i = 0; i < 32; ++i)
                if ((raw & ~prevRaw) & (1u << i)) shim_log("wheel: button %d pressed", i);
            prevRaw = raw;
            DWORD pov = s.rgdwPOV[0];
            if (LOWORD(pov) != 0xFFFF) ub |= pad_hat_bits((int)((pov + 2250) / 4500) % 8);
            *buttons = ub;
            ffb_update();
            ok = true;
        } else if (hr == DIERR_UNPLUGGED || hr == E_HANDLE) {
            drop_device("unplugged");
        } else {
            *steer = *gas = *brake = 0; *buttons = 0;   // unfocused: neutral, still "present"
            ok = true;
        }
    }
    LeaveCriticalSection(&g_wcs);
    return ok;
}

void wheel_attach(void)
{
    InitializeCriticalSection(&g_wcs);
}
