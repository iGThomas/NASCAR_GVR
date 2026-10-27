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
//  Devices - set up by themselves; the [Wheel] settings only override a wrong guess:
//    WHEEL     steering, buttons, force feedback: [Wheel] Device=, else a force-feedback
//              driving device, else any force-feedback device (a direct-drive base that
//              does not call itself a wheel), else any driving device; never a pedal box.
//    PEDALS    on the wheel, or a pedal box on its own USB cable: [Wheel] PedalDevice=,
//              else a name with "pedal" / "LCM", else LEARNED: for a wheel that is not a
//              known model, every device next to it is watched and the first one whose
//              axis rests at an END of its range and then travels far (a pedal; a stick or
//              wheel axis rests in the middle) becomes the pedal box (pedal_moved). With a
//              pedal box but no wheel, steering stays on the pad / keyboard.
//    SHIFTER   on its own USB cable: [Wheel] ShifterDevice=, else "shift" / "TH8" / "TSS",
//              else learned (box_is_shifter): a small device with no axes whose button
//              stays held over 3 s (a gear) - a button box never holds one that long.
//              H-pattern or sequential is learned from use (shifter_bits): a held gear =
//              H (buttons 0..5 = gears 1, 2, 3, 4, 4, 4), short taps = sequential (0 / 1 =
//              square / triangle = shift up / down). ShifterMode = h | sequential forces it.
//    BUTTONS   every other controller next to the wheel that is not a gamepad - a separate
//              rim, a button box: its buttons act like the wheel's (ButtonN). Slots 1..3
//              can be named in ButtonBox1..3 (with ButtonBoxKButtonN maps).
//  Every attached controller is listed in the log, then one "controls:" line says what
//  each device became (again whenever something is learned). All are read with our OWN
//  IDirectInput8 - which shares DirectInput's vtables with GvrIO's, so GvrIOPad.cpp's
//  GetDeviceState hook must skip them (wheel_owns).
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
//  Pedal calibration - the cabinet's own: the I/O board keeps a raw position and a min / max
//  per pedal, the shell shows "Calibrate the accelerator and brake" when max - min < 40 and the
//  operator Control Test resets it (Start + Music). The shim plays that board for PC pedals
//  (wheel_cal_axis / wheel_cal_reset, GvrIOShim.cpp msgs 6 / 0x39 / 0x3A / 0x53) and keeps the
//  min / max in <install>\nascar_calibration.ini for the pedal device, so the shell and every
//  race use it; a new pedal device starts uncalibrated and the shell asks once. Like the board,
//  max keeps growing when a pedal goes deeper. Until a pedal is calibrated, 100 % is the deepest
//  point reached this session (at least half the axis).
//
//  Force feedback - [Wheel] ForceFeedback:
//    arcade (default)  the OEM GvrIO plug-in Steering.dll does it, exactly as on the
//                      cabinet (for a force-feedback base that does not report itself as
//                      a driving device Steering.dll finds nothing: then ours, as shim);
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
static LPDIRECTINPUTDEVICE8A g_pdev = NULL;   // separate pedal box (NULL: pedals on the wheel)
static char  g_pname[MAX_PATH] = "";
static HWND  g_pCoopWnd = NULL;
static bool  g_pAcquired = false;
// Pedal learning per device: which axes it has and where they rested at its first real report.
struct Learn { unsigned present; bool real; long rest[8]; };
// Devices next to the wheel: [0] a separate gear shifter, [1..6] button boxes / separate rims
// (slots 1..3 can be named in [Wheel] ButtonBox1..3; other controllers fill free slots).
struct ExtraDev { LPDIRECTINPUTDEVICE8A dev; char name[MAX_PATH]; HWND coop; bool acq;
                  unsigned map[32]; unsigned prev; char filter[64]; DWORD id; Learn learn;
                  DWORD nbtn; DWORD heldSince; };
enum { X_SHIFTER = 0, X_NAMED = 4, X_COUNT = 7 };
static ExtraDev g_x[X_COUNT];
static const char* const x_role[X_COUNT] = { "shifter", "buttons 1", "buttons 2", "buttons 3",
                                             "buttons 4", "buttons 5", "buttons 6" };
enum { SH_AUTO, SH_H, SH_SEQ };
static int   g_shiftCfg = SH_AUTO;            // [Wheel] ShifterMode
static unsigned g_seqMap[32];                 // the shifter used sequentially: button -> pad bit
static bool  g_pedalLearn = false;            // pedals not settled yet: watch the devices (pedal_moved)
static Learn g_wLearn;                        // the wheel's axes, for that
static const char* g_pedalHow = "on the wheel";

// ---------------------------------------------------------------- settings ([Wheel])
enum { AX_X, AX_Y, AX_Z, AX_RX, AX_RY, AX_RZ, AX_S0, AX_S1, AX_NONE, AX_AUTO = -2 };
enum { PD_AUTO, PD_SEPARATE, PD_COMBINED };
static int   g_axSteer = AX_X, g_axGas = AX_AUTO, g_axBrake = AX_AUTO;   // as set in [Wheel]
static int   g_pedalsCfg = PD_AUTO;
static bool  g_swapPedals = false;
static bool  g_oldDefault = false;            // the v1.0.9 template's Gas = y / Brake = rz, ignored
// Developer test only (GVRIOSHIM_TEST_PAD_AS_WHEEL=1): a gamepad stands in for a wheel - its
// triggers for pedals ([Wheel] Gas = ry / Brake = rx on a DS4) - so the calibration flow can be
// tested without a wheel. Its buttons are left to the pad code.
static bool  g_testPad = false;
static volatile bool g_testFed = false;
static volatile int  g_testSteer = 0, g_testGas = 0, g_testBrake = 0;

void wheel_test_feed(bool present, int steer, int gas, int brake)
{
    if (!g_testPad || !present) return;
    g_testSteer = steer; g_testGas = gas; g_testBrake = brake; g_testFed = true;
}
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
static char  g_pedalFilter[64] = "";
// force feedback (percent)
// shim force feedback (percent); defaults = the arcade's "Normal" Steering Feedback Strength
static int   g_ffbGain = 100, g_ffbSpring = 40, g_ffbDamper = 40, g_ffbForce = 100;
static bool  g_ffbOn = false;                 // ours: ForceFeedback = shim, or arcade on a non-wheel base
enum { FF_ARCADE, FF_SHIM, FF_OFF };
static int   g_ffbSetting = FF_ARCADE;

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
    g_testPad = GetEnvironmentVariableA("GVRIOSHIM_TEST_PAD_AS_WHEEL", NULL, 0) > 0;
    if (g_testPad) shim_log("wheel: TEST - a gamepad stands in for the wheel (GVRIOSHIM_TEST_PAD_AS_WHEEL)");
    lstrcpynA(g_devFilter, S("Device", ""), sizeof g_devFilter);
    lstrcpynA(g_pedalFilter, S("PedalDevice", ""), sizeof g_pedalFilter);
    lstrcpynA(g_x[X_SHIFTER].filter, S("ShifterDevice", ""), sizeof g_x[0].filter);
    for (int b = 1; b < X_NAMED; ++b) {
        char key[16]; _snprintf(key, sizeof key, "ButtonBox%d", b); key[15] = 0;
        lstrcpynA(g_x[b].filter, S(key, ""), sizeof g_x[b].filter);
    }
    S("ShifterMode", "auto");
    g_shiftCfg = _stricmp(v, "sequential") == 0 ? SH_SEQ : _stricmp(v, "h") == 0 ? SH_H : SH_AUTO;
    if (g_shiftCfg == SH_AUTO && _stricmp(v, "auto") != 0 && v[0])
        shim_log("wheel cfg: ShifterMode = '%s' - use auto, h or sequential", v);
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
    g_ffbSetting = _stricmp(v, "shim") == 0 ? FF_SHIM : _stricmp(v, "off") == 0 ? FF_OFF : FF_ARCADE;
    g_ffbOn      = g_ffbSetting == FF_SHIM;
    if (g_ffbSetting == FF_ARCADE && _stricmp(v, "arcade") != 0)
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
    // the shifter: H-pattern buttons 0..5 = gears 1..6 (the arcade shifter has 4: 5th, 6th = 4th);
    // sequential: button 0 / 1 = square / triangle (= shift up / down in [Controller]); a
    // ShifterButtonN setting counts for both. A button box / separate rim: the wheel's ButtonN
    // map, then (named slots 1..3) its own ButtonBoxKButtonN keys.
    static const char* hdefs[6] = { "gear1", "gear2", "gear3", "gear4", "gear4", "gear4" };
    for (int b = 0; b < X_COUNT; ++b) {
        ExtraDev& x = g_x[b];
        for (int i = 0; i < 32; ++i) {
            if (b != X_SHIFTER) x.map[i] = g_btnMap[i];
            else {
                x.map[i] = i < 6 ? pad_button_bit(hdefs[i]) : 0;
                g_seqMap[i] = i == 0 ? pad_button_bit("square") : i == 1 ? pad_button_bit("triangle") : 0;
            }
            if (b >= X_NAMED) continue;            // automatic slots: the wheel's map only
            char key[32];
            if (b == X_SHIFTER) _snprintf(key, sizeof key, "ShifterButton%d", i);
            else _snprintf(key, sizeof key, "ButtonBox%dButton%d", b, i);
            key[31] = 0;
            GetPrivateProfileStringA("Wheel", key, "", v, sizeof v, ini);
            if (!v[0]) continue;
            x.map[i] = _stricmp(v, "none") == 0 ? 0 : pad_button_bit(v);
            if (b == X_SHIFTER) g_seqMap[i] = x.map[i];
            if (!x.map[i] && _stricmp(v, "none") != 0)
                shim_log("wheel cfg: %s = '%s' is not a pad button name - ignored", key, v);
        }
    }
    #undef S
    shim_log("wheel cfg: steer=%s gas=%s brake=%s pedals=%s%s%s rotation=%d/%d force feedback=%s (gain=%d spring=%d damper=%d)",
             ax_name(g_axSteer), ax_name(g_axGas), ax_name(g_axBrake),
             g_pedalsCfg == PD_COMBINED ? "combined" : g_pedalsCfg == PD_SEPARATE ? "separate" : "auto",
             g_swapPedals ? " swapped" : "", g_oldDefault ? " (old Gas = y / Brake = rz default ignored)" : "",
             g_rotation, g_range,
             g_ffbSetting == FF_SHIM ? "shim" : g_ffbSetting == FF_OFF ? "off" : "arcade (ours for a base that is not a driving device)",
             g_ffbGain, g_ffbSpring, g_ffbDamper);
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
// Every attached game controller (the names are what [Wheel] Device / PedalDevice take).
struct DevInfo { GUID guid; DWORD product; BYTE type; bool ffb; char name[MAX_PATH]; };
struct DevList { DevInfo d[16]; int n; };

static BOOL CALLBACK list_cb(LPCDIDEVICEINSTANCEA d, LPVOID p)
{
    DevList* l = (DevList*)p;
    if (l->n >= 16) return DIENUM_STOP;
    DevInfo& e = l->d[l->n++];
    e.guid = d->guidInstance;
    e.product = d->guidProduct.Data1;             // MAKELONG(vendor, product) for USB devices
    e.type = GET_DIDEVICE_TYPE(d->dwDevType);
    e.ffb = false;
    lstrcpynA(e.name, d->tszProductName, sizeof e.name);
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK ffb_cb(LPCDIDEVICEINSTANCEA d, LPVOID p)
{
    DevList* l = (DevList*)p;
    for (int i = 0; i < l->n; ++i)
        if (IsEqualGUID(l->d[i].guid, d->guidInstance)) l->d[i].ffb = true;
    return DIENUM_CONTINUE;
}

static bool has_text(const char* s, const char* sub)    // case-insensitive
{
    size_t n = strlen(sub);
    for (; n && *s; ++s) if (_strnicmp(s, sub, n) == 0) return true;
    return false;
}

// A gamepad - read by the pad code (GvrIOPad.cpp), never a wheel part or a button box. DirectInput
// types a PS4 pad as "other", not gamepad, so the pad makers count too: Sony, Nintendo, Microsoft.
static bool pad_like(const DevInfo& e)
{
    WORD vid = LOWORD(e.product);
    return e.type == DI8DEVTYPE_GAMEPAD || vid == 0x054C || vid == 0x057E || vid == 0x045E;
}

static bool pedal_like(const DevInfo& e) { return has_text(e.name, "pedal") || has_text(e.name, "LCM"); }
static bool shifter_like(const DevInfo& e)
{
    const char* f = g_x[X_SHIFTER].filter;
    return f[0] ? has_text(e.name, f)
                : has_text(e.name, "shift") || has_text(e.name, "TH8") || has_text(e.name, "TSS");
}
static bool box_like(const DevInfo& e)            // named in [Wheel] ButtonBox1..3
{
    for (int b = 1; b < X_NAMED; ++b) if (g_x[b].filter[0] && has_text(e.name, g_x[b].filter)) return true;
    return false;
}

// The wheel: [Wheel] Device, else force-feedback driving, else force feedback (a direct-drive
// base may call itself a joystick; pedal boxes have no force feedback; rumble gamepads do not
// count), else driving - never a pedal box. -1 = none.
static int pick_wheel(const DevList& l)
{
    if (g_testPad)
        for (int i = 0; i < l.n; ++i) if (pad_like(l.d[i])) return i;
    if (g_devFilter[0]) {
        for (int i = 0; i < l.n; ++i) if (has_text(l.d[i].name, g_devFilter)) return i;
        return -1;
    }
    for (int pass = 0; pass < 3; ++pass)
        for (int i = 0; i < l.n; ++i) {
            const DevInfo& e = l.d[i];
            if (pedal_like(e) || shifter_like(e) || box_like(e) || (g_pedalFilter[0] && has_text(e.name, g_pedalFilter))) continue;
            bool driving = e.type == DI8DEVTYPE_DRIVING;
            if (pass == 0 ? driving && e.ffb : pass == 1 ? e.ffb && !pad_like(e) : driving) return i;
        }
    return -1;
}

// A separate pedal box: [Wheel] PedalDevice, else a name with "pedal" / "LCM". -1 = none.
static int pick_pedals(const DevList& l, int wheel)
{
    for (int i = 0; i < l.n; ++i)
        if (i != wheel && !box_like(l.d[i]) &&
            (g_pedalFilter[0] ? has_text(l.d[i].name, g_pedalFilter) : pedal_like(l.d[i])))
            return i;
    return -1;
}

// A separate gear shifter (only next to a wheel): [Wheel] ShifterDevice, else "shift" / "TH8" /
// "TSS" in the name. -1 = none.
static int pick_shifter(const DevList& l, int wheel, int pedals)
{
    if (wheel < 0) return -1;
    for (int i = 0; i < l.n; ++i)
        if (i != wheel && i != pedals && !pedal_like(l.d[i]) && !box_like(l.d[i]) && shifter_like(l.d[i])) return i;
    return -1;
}

// Button box b (1..3, only next to a wheel): the first device named in [Wheel] ButtonBoxb that is
// none of the others. -1 = none.
static int pick_box(const DevList& l, int b, const int* taken, int ntaken)
{
    if (taken[0] < 0 || !g_x[b].filter[0]) return -1;
    for (int i = 0; i < l.n; ++i) {
        bool used = false;
        for (int t = 0; t < ntaken; ++t) if (taken[t] == i) used = true;
        if (!used && has_text(l.d[i].name, g_x[b].filter)) return i;
    }
    return -1;
}

// Every other controller next to the wheel that is not a gamepad (pads are read by GvrIOPad):
// a free button slot each - a separate rim, a button box (or a pedal box, found by pedal_moved).
// pick[0] wheel, [1] pedals, [2 + k] slot k. `pedalName` = an open pedal box's name.
static void assign_auto_boxes(const DevList& l, int* pick, const char* pedalName)
{
    if (pick[0] < 0) return;
    for (int i = 0; i < l.n; ++i) {
        bool used = pad_like(l.d[i]) || (pedalName[0] && !strcmp(l.d[i].name, pedalName));
        for (int t = 0; t < 2 + X_COUNT; ++t) if (pick[t] == i) used = true;
        if (used) continue;
        for (int b = 1; b < X_COUNT; ++b)
            if (pick[2 + b] < 0 && !g_x[b].dev && (b >= X_NAMED || !g_x[b].filter[0])) { pick[2 + b] = i; break; }
    }
}

// Every axis reports 0..65535 over its WHOLE travel: no dead zone, and saturation 100 % - a
// lower saturation (left in Windows' game controller settings) reads full before the pedal
// is down.
static void set_range(LPDIRECTINPUTDEVICE8A dev, DWORD ofs)
{
    DIPROPRANGE r; r.diph.dwSize = sizeof r; r.diph.dwHeaderSize = sizeof r.diph;
    r.diph.dwObj = ofs; r.diph.dwHow = DIPH_BYOFFSET; r.lMin = 0; r.lMax = 65535;
    dev->SetProperty(DIPROP_RANGE, &r.diph);
    DIPROPDWORD dz; dz.diph.dwSize = sizeof dz; dz.diph.dwHeaderSize = sizeof dz.diph;
    dz.diph.dwObj = ofs; dz.diph.dwHow = DIPH_BYOFFSET; dz.dwData = 0;
    dev->SetProperty(DIPROP_DEADZONE, &dz.diph);
    dz.dwData = 10000;
    dev->SetProperty(DIPROP_SATURATION, &dz.diph);
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

// Pedal reading state (see wheel_read); reset for every newly chosen pedal source.
struct PedalState { long rest; unsigned logged; long travel, travelLogged; int which; };

// The cabinet-style calibration (see the header): [1] gas, [2] brake; raw / min / max 0..255.
struct PedalCal { int raw, min, max; };
static PedalCal g_cal[3];
static DWORD g_calId = 0;                     // the pedal source it belongs to (0: none yet)
static bool  g_calDirty = false;
static DWORD g_calSaveTick = 0;

static bool cal_path(char* out, size_t n)
{
    char ini[MAX_PATH];
    if (!shim_settings_path(ini, sizeof ini)) return false;
    char* sl = strrchr(ini, '\\');
    if (!sl) return false;
    *sl = 0;
    _snprintf(out, n, "%s\\nascar_calibration.ini", ini);
    out[n - 1] = 0;
    return true;
}

static bool cal_valid(int which) { return g_cal[which].max - g_cal[which].min >= 40; }

// Under g_wcs: the pedal source is `id` - use its saved calibration, or start uncalibrated.
static void cal_attach(DWORD id)
{
    g_calId = id ? id : 1;
    memset(g_cal, 0, sizeof g_cal);
    g_calDirty = false;
    char path[MAX_PATH], v[32];
    if (!cal_path(path, sizeof path)) return;
    GetPrivateProfileStringA("Pedals", "Device", "", v, sizeof v, path);
    if (strtoul(v, NULL, 16) != g_calId) {
        shim_log("pedals: not calibrated for this pedal device yet - the shell asks once (operator Control Test: "
                 "Start + Music calibrates again)");
        return;
    }
    g_cal[1].min = GetPrivateProfileIntA("Pedals", "GasMin", 0, path);
    g_cal[1].max = GetPrivateProfileIntA("Pedals", "GasMax", 0, path);
    g_cal[2].min = GetPrivateProfileIntA("Pedals", "BrakeMin", 0, path);
    g_cal[2].max = GetPrivateProfileIntA("Pedals", "BrakeMax", 0, path);
    shim_log("pedals: calibration loaded - gas %d..%d, brake %d..%d (of 255)",
             g_cal[1].min, g_cal[1].max, g_cal[2].min, g_cal[2].max);
}

// Under g_wcs: write the calibration when it changed (at most every 2 s).
static void cal_save(DWORD now)
{
    if (!g_calDirty || now - g_calSaveTick < 2000 || !g_calId) return;
    g_calSaveTick = now; g_calDirty = false;
    char path[MAX_PATH], v[32];
    if (!cal_path(path, sizeof path)) return;
    _snprintf(v, sizeof v, "%08lX", (unsigned long)g_calId);
    WritePrivateProfileStringA("Pedals", "Device", v, path);
    static const char* key[3][2] = { { 0, 0 }, { "GasMin", "GasMax" }, { "BrakeMin", "BrakeMax" } };
    for (int w = 1; w <= 2; ++w)
        for (int m = 0; m < 2; ++m) {
            _snprintf(v, sizeof v, "%d", m ? g_cal[w].max : g_cal[w].min);
            WritePrivateProfileStringA("Pedals", key[w][m], v, path);
        }
    shim_log("pedals: calibration saved - gas %d..%d, brake %d..%d (of 255)",
             g_cal[1].min, g_cal[1].max, g_cal[2].min, g_cal[2].max);
}
static PedalState g_pg, g_pb;                 // gas, brake (combined: g_pg holds the axis' rest)
static bool  g_realReport = false;            // the pedal source has sent a report of its own
// "axis moves" logging, per device (wheel, pedal box)
struct AxLog { long start[8]; bool have, logged[8]; unsigned present; bool real; };
static AxLog g_wAx, g_pAx;

static void pedals_reset(void)
{
    g_pg.rest = g_pb.rest = -1; g_pg.logged = g_pb.logged = 0;
    g_pg.travel = g_pb.travel = g_pg.travelLogged = g_pb.travelLogged = 0;
    g_pg.which = 1; g_pb.which = 2;
    g_realReport = false;
}

static bool end_band(long v) { return v >= 0 && (v < 65535 / 10 || v > 65535 - 65535 / 10); }

static const WheelPreset* find_preset(DWORD id)
{
    for (size_t i = 0; i < sizeof g_presets / sizeof g_presets[0]; ++i)
        if (g_presets[i].id == id) return &g_presets[i];
    return NULL;
}

// Under g_wcs: H-shifter gears are buttons of the wheel: gear 1..6 (the arcade shifter has 4
// positions: 5th, 6th = 4th).
static void choose_shifter(DWORD id, const char* devName)
{
    const WheelPreset* p = find_preset(id);
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

// Under g_wcs: decide the pedal layout of the pedal source just chosen (the wheel, or the
// separate pedal box); g_axPresent = that device's axes. `lr` (may be NULL): where its axes
// rested, when known - then only axes resting at an END are pedals. `skip` = the steering axis.
static void choose_layout(DWORD id, const Learn* lr, int skip)
{
    const WheelPreset* p = find_preset(id);
    int gas = AX_RZ, brake = AX_Y;
    bool comb = false;
    const char* from = "default - not a known model";
    unsigned cand = 0;                            // its pedal axes
    for (int a = 0; a < 8; ++a)
        if (a != skip && (g_axPresent & (1u << a)) && (!lr || !lr->real || end_band(lr->rest[a]))) cand |= 1u << a;
    if (p) { gas = p->gas; brake = p->brake; comb = p->combined; from = p->name; }
    else if (!lr && (g_axPresent & (1u << AX_Y)) && !(g_axPresent & ((1u << AX_Z) | (1u << AX_RZ)))) {
        gas = brake = AX_Y; comb = true; from = "Y is its only pedal axis";
    } else if ((cand & (1u << AX_Y)) && (cand & (1u << AX_RZ))) {
        from = "default order: gas rz, brake y";
    } else if (cand) {                            // its pedal axes in order: gas, brake (, clutch)
        int found[2] = { AX_NONE, AX_NONE }, n = 0;
        for (int a = 0; a < 8 && n < 2; ++a) if (cand & (1u << a)) found[n++] = a;
        gas = found[0]; brake = found[1];
        from = "its pedal axes in order: gas, brake";
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
    if (lr && lr->real) {                         // rests already known: no guessing from a pressed pedal
        g_combinedAuto = false;
        g_realReport = true;
        if (!comb) { g_pg.rest = lr->rest[gas < 8 ? gas : 0]; g_pb.rest = brake < 8 ? lr->rest[brake] : -1; }
    }
    cal_attach(id);
    if (comb)
        shim_log("wheel: pedals combined on '%s', gas pulls it %s (%s)", ax_name(gas),
                 g_gasDown ? "down" : "up", from);
    else
        shim_log("wheel: pedals gas = '%s', brake = '%s'%s (%s)%s", ax_name(gas), ax_name(brake),
                 g_swapPedals ? " (swapped)" : "", from,
                 p && !p->sure ? " - layout not yet confirmed on Windows: if gas brakes, set [Wheel] SwapPedals = true" : "");
}

// Sequential or H-pattern, learned from use (ShifterMode = auto): a gear button held over
// 0.4 s means H (gears), two short taps without such a hold mean sequential (shift up / down).
// Until it is known a press waits up to 0.4 s; a tap is passed on as a 150 ms shift press.
struct ShiftState { int mode; int taps; DWORD down[32]; DWORD pulse[32]; unsigned held; };
static ShiftState g_sh;

static unsigned shifter_bits(ShiftState& st, unsigned raw, DWORD now, const unsigned* hmap,
                             const unsigned* smap, int* learned)
{
    *learned = SH_AUTO;
    unsigned out = 0;
    for (int i = 0; i < 32; ++i) {
        unsigned b = 1u << i;
        bool on = (raw & b) != 0, was = (st.held & b) != 0;
        if (on && !was) st.down[i] = now;
        if (st.mode == SH_AUTO) {
            if (on && now - st.down[i] >= 400) { st.mode = SH_H; *learned = SH_H; }
            else if (!on && was && now - st.down[i] < 400) {
                st.pulse[i] = now + 150;
                if (++st.taps >= 2) { st.mode = SH_SEQ; *learned = SH_SEQ; }
            }
        }
        if (st.pulse[i] && (int)(st.pulse[i] - now) > 0) out |= smap[i]; else st.pulse[i] = 0;
    }
    st.held = raw;
    if (st.mode == SH_H) {
        out = 0;
        for (int i = 0; i < 32; ++i) if (raw & (1u << i)) out |= hmap[i];
    } else if (st.mode == SH_SEQ) {
        for (int i = 0; i < 32; ++i) if (raw & (1u << i)) out |= smap[i];
    }
    return out;
}

// One log line saying what every device became - what a player sends when a guess is wrong.
static void controls_summary(void)
{
    char buf[1024]; size_t n = 0;
    #define ADD(...) do { int r_ = _snprintf(buf + n, sizeof buf - n - 1, __VA_ARGS__); \
                          n = r_ < 0 ? sizeof buf - 1 : n + r_; buf[n] = 0; } while (0)
    buf[0] = 0;
    ADD("controls: wheel %s%s%s", g_dev ? "'" : "", g_dev ? g_name : "none", g_dev ? "'" : "");
    if (g_pdev) ADD(" | pedals '%s' (%s)", g_pname, g_pedalHow);
    else ADD(" | pedals on the wheel%s", g_pedalLearn ? " (watching the other devices for pedals)" : "");
    if (g_x[X_SHIFTER].dev)
        ADD(" | shifter '%s' (%s)", g_x[X_SHIFTER].name,
            g_sh.mode == SH_H ? "H-pattern" : g_sh.mode == SH_SEQ ? "sequential" : "H or sequential: learning");
    for (int k = 1; k < X_COUNT; ++k) if (g_x[k].dev) ADD(" | buttons '%s'", g_x[k].name);
    ADD(" | force feedback: %s", g_ffbSetting == FF_OFF ? "off" : g_ffbOn ? "ours" : "arcade");
    #undef ADD
    shim_log("%s", buf);
}

// Discovery: the pad thread every ~1.5 s while there is no wheel, and once from the shell's
// startup pedal check (wheel_discover_first) - serialised.
static CRITICAL_SECTION g_dcs;
static void discover(void);
static volatile LONG g_discovered = 0;

void wheel_discover(void)
{
    EnterCriticalSection(&g_dcs);
    discover();
    InterlockedExchange(&g_discovered, 1);
    LeaveCriticalSection(&g_dcs);
}

// The shell asks for the pedal range at start-up, possibly before the pad thread's first pass.
void wheel_discover_first(void)
{
    if (!g_discovered) wheel_discover();
}

static void discover(void)
{
    static bool configured = false;
    if (!configured) {
        configured = true;
        g_woff = GetEnvironmentVariableA("GVRIOSHIM_NO_WHEEL", NULL, 0) > 0;
        if (!g_woff) load_wheel_config();
        if (g_woff) shim_log("wheel: off");
    }
    if (g_woff || g_dev) return;                  // (with only a pedal box: keep looking for a wheel)
    if (!g_di) {
        HMODULE h = LoadLibraryA("dinput8.dll");
        PFN_DI8Create create = h ? (PFN_DI8Create)GetProcAddress(h, "DirectInput8Create") : NULL;
        if (!create || FAILED(create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8A, (void**)&g_di, NULL))) {
            shim_log("wheel: DirectInput unavailable - wheel support off");
            g_woff = true;
            return;
        }
    }
    DevList l; memset(&l, 0, sizeof l);
    g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, list_cb, &l, DIEDFL_ATTACHEDONLY);
    g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, ffb_cb, &l, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    // the controller list, whenever it changes - which device is which, for [Wheel] Device / PedalDevice
    static DWORD lastSig = 0xFFFFFFFF;
    DWORD sig = (DWORD)l.n;
    for (int i = 0; i < l.n; ++i) sig = sig * 31 + l.d[i].product + l.d[i].type * 7 + l.d[i].ffb;
    if (sig != lastSig) {
        lastSig = sig;
        shim_log("controllers: %d attached", l.n);
        for (int i = 0; i < l.n; ++i)
            shim_log("controllers: %d '%s' (%04x:%04x, %s, force feedback %s)", i + 1, l.d[i].name,
                     LOWORD(l.d[i].product), HIWORD(l.d[i].product),
                     l.d[i].type == DI8DEVTYPE_DRIVING ? "wheel" : l.d[i].type == DI8DEVTYPE_GAMEPAD ? "gamepad" :
                     l.d[i].type == DI8DEVTYPE_JOYSTICK ? "joystick" : pad_like(l.d[i]) ? "gamepad by maker" : "other type",
                     l.d[i].ffb ? "yes" : "no");
    }
    int wi = pick_wheel(l);
    int pi = g_pdev ? -1 : pick_pedals(l, wi);
    // pick[0] wheel, [1] pedals, [2 + k] extra device k (shifter, button boxes)
    enum { NPICK = 2 + X_COUNT };
    int pick[NPICK];
    pick[0] = wi; pick[1] = pi;
    pick[2 + X_SHIFTER] = g_x[X_SHIFTER].dev ? -1 : pick_shifter(l, wi, pi);
    for (int b = 1; b < X_COUNT; ++b) pick[2 + b] = g_x[b].dev || b >= X_NAMED ? -1 : pick_box(l, b, pick, 2 + b);
    assign_auto_boxes(l, pick, g_pdev ? g_pname : "");
    if (wi < 0 && pi < 0) return;

    static const DWORD ofs[8] = { DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ,
                                  DIJOFS_SLIDER(0), DIJOFS_SLIDER(1) };
    LPDIRECTINPUTDEVICE8A dev[NPICK]; DWORD id[NPICK]; unsigned present[NPICK]; bool ffb[NPICK]; DWORD nbtn[NPICK];
    memset(dev, 0, sizeof dev); memset(id, 0, sizeof id); memset(present, 0, sizeof present); memset(ffb, 0, sizeof ffb);
    memset(nbtn, 0, sizeof nbtn);
    for (int k = 0; k < NPICK; ++k) {
        int i = pick[k];
        if (i < 0) continue;
        const DevInfo& e = l.d[i];
        LPDIRECTINPUTDEVICE8A d = NULL;
        if (FAILED(g_di->CreateDevice(e.guid, &d, NULL))) continue;
        if (FAILED(d->SetDataFormat(&c_dfDIJoystick2))) { d->Release(); continue; }
        DIDEVCAPS caps; caps.dwSize = sizeof caps;
        if (FAILED(d->GetCapabilities(&caps))) memset(&caps, 0, sizeof caps);
        ffb[k] = (caps.dwFlags & DIDC_FORCEFEEDBACK) != 0;
        nbtn[k] = caps.dwButtons;
        DIPROPDWORD vp; vp.diph.dwSize = sizeof vp; vp.diph.dwHeaderSize = sizeof vp.diph;
        vp.diph.dwObj = 0; vp.diph.dwHow = DIPH_DEVICE; vp.dwData = 0;
        id[k] = SUCCEEDED(d->GetProperty(DIPROP_VIDPID, &vp.diph)) && vp.dwData ? vp.dwData : e.product;
        char axes[64] = "";
        for (int a = 0; a < 8; ++a) {
            DIDEVICEOBJECTINSTANCEA oi; oi.dwSize = sizeof oi;
            if (SUCCEEDED(d->GetObjectInfo(&oi, ofs[a], DIPH_BYOFFSET))) {
                present[k] |= 1u << a;
                strcat(axes, " "); strcat(axes, s_axName[a]);
            }
            set_range(d, ofs[a]);
        }
        shim_log("%s: '%s' found (%04x:%04x, %lu axes:%s, %lu buttons, force feedback %s)",
                 k == 0 ? "wheel" : k == 1 ? "pedals" : x_role[k - 2], e.name, LOWORD(id[k]), HIWORD(id[k]), caps.dwAxes, axes,
                 caps.dwButtons, ffb[k] ? "yes" : "no");
        dev[k] = d;
    }
    for (int k = 2; k < NPICK; ++k)                // shifter / button boxes only with a wheel
        if (dev[k] && !dev[0]) { dev[k]->Release(); dev[k] = NULL; }
    if (!dev[0] && !dev[1]) return;

    EnterCriticalSection(&g_wcs);
    if (dev[1]) {                                 // separate pedal box: the pedals come from it
        g_pdev = dev[1]; g_pCoopWnd = NULL; g_pAcquired = false;
        lstrcpynA(g_pname, l.d[pi].name, sizeof g_pname);
        memset(&g_pAx, 0, sizeof g_pAx); g_pAx.present = present[1];
        g_axPresent = present[1];
        pedals_reset();
        g_pedalHow = g_pedalFilter[0] ? "PedalDevice" : "by name";
        choose_layout(id[1], NULL, -1);
        if (!dev[0]) shim_log("pedals: no steering wheel found - steering stays on the pad / keyboard");
    }
    if (dev[0]) {
        g_dev = dev[0]; g_ffb = ffb[0]; g_coopWnd = NULL; g_acquired = false;
        lstrcpynA(g_name, l.d[wi].name, sizeof g_name);
        memset(&g_wAx, 0, sizeof g_wAx); g_wAx.present = present[0];
        memset(&g_wLearn, 0, sizeof g_wLearn); g_wLearn.present = present[0];
        if (!g_pdev) {                            // pedals on the wheel - unless another device turns out to have them
            g_axPresent = present[0];
            pedals_reset();
            choose_layout(id[0], NULL, g_axSteer);
            g_pedalLearn = !find_preset(id[0]) && !g_pedalFilter[0] && g_axGas == AX_AUTO && g_pedalsCfg == PD_AUTO;
            g_pedalHow = "on the wheel";
        } else {
            shim_log("wheel: pedals read from '%s', not from the wheel", g_pname);
        }
        // force feedback: Steering.dll (arcade) only drives devices that report as a wheel
        if (g_ffbSetting == FF_ARCADE && ffb[0] && l.d[wi].type != DI8DEVTYPE_DRIVING) {
            g_ffbOn = true;
            shim_log("wheel: force feedback from us - the arcade's only drives devices that report as a wheel");
        }
        choose_shifter(id[0], g_name);
        for (int k = 0; k < X_COUNT; ++k) {        // shifter / button boxes: their buttons add to the wheel's
            if (!dev[2 + k]) continue;
            ExtraDev& x = g_x[k];
            x.dev = dev[2 + k]; x.coop = NULL; x.acq = false; x.prev = 0; x.id = id[2 + k];
            x.nbtn = nbtn[2 + k]; x.heldSince = 0;
            memset(&x.learn, 0, sizeof x.learn); x.learn.present = present[2 + k];
            lstrcpynA(x.name, l.d[pick[2 + k]].name, sizeof x.name);
            if (k == X_SHIFTER) {
                memset(&g_sh, 0, sizeof g_sh); g_sh.mode = g_shiftCfg;
                shim_log("shifter: buttons 0..5 = gear 1, 2, 3, 4, 4, 4 when held (H-pattern), 0 / 1 = square / triangle "
                         "(shift up / down) when tapped (sequential) - %s; ShifterButtonN overrides; gears act with Manual "
                         "chosen on the transmission screen",
                         g_shiftCfg == SH_H ? "set to H" : g_shiftCfg == SH_SEQ ? "set to sequential" : "learned from the first presses");
            }
        }
    }
    if (g_dev || g_pdev) controls_summary();
    LeaveCriticalSection(&g_wcs);
}

bool wheel_owns(void* device)
{
    if (!device) return false;
    if (device == (void*)g_dev || device == (void*)g_pdev) return true;
    for (int k = 0; k < X_COUNT; ++k) if (device == (void*)g_x[k].dev) return true;
    return false;
}

bool wheel_present(void)
{
    return !g_woff && (g_dev != NULL || g_pdev != NULL);
}

bool wheel_steers(void)
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

static void drop_pedals(const char* why)
{
    if (g_pdev) { g_pdev->Unacquire(); g_pdev->Release(); g_pdev = NULL; }
    g_pAcquired = false; g_pCoopWnd = NULL;
    shim_log("pedals: '%s' closed (%s)", g_pname, why);
}

static void drop_extra(int k, const char* why)
{
    ExtraDev& x = g_x[k];
    if (!x.dev) return;
    x.dev->Unacquire(); x.dev->Release(); x.dev = NULL;
    x.acq = false; x.coop = NULL;
    shim_log("%s: '%s' closed (%s)", x_role[k], x.name, why);
}

// The pedal box / shifter: shared access, on the root of our own foreground window. Called
// only while this process has focus.
static bool ensure_shared_acquired(LPDIRECTINPUTDEVICE8A d, HWND* coop, bool* acquired)
{
    HWND fg = GetForegroundWindow();
    HWND root = fg ? GetAncestor(fg, GA_ROOT) : NULL;
    if (!root) return false;
    if (root != *coop) {
        d->Unacquire(); *acquired = false;
        if (FAILED(d->SetCooperativeLevel(root, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))) return false;
        *coop = root;
    }
    if (!*acquired) {
        if (FAILED(d->Acquire())) return false;
        *acquired = true;
    }
    return true;
}

// Which axis is which differs per device and driver: log each axis once, the first time it
// moves well away from where it was at the device's first real report (its name is what
// [Wheel] Steering / Gas / Brake take).
static long axis_val(const DIJOYSTATE2& s, int ax);

// Pedal learning: a pedal axis rests at one END of its range and travels far when pressed; a
// steering or stick axis rests in the middle. Feed every reading; returns the axis that just
// moved like a pedal (-1: none). A device counts only with >= minEnds axes resting at an end.
static int pedal_moved(Learn& lr, const DIJOYSTATE2& s, int skip, int minEnds)
{
    if (!lr.real) {                               // DirectInput shows mid-axis until the first report
        bool r = false;
        for (int a = 0; a < 8; ++a) if ((lr.present & (1u << a)) && labs(axis_val(s, a) - 32767) > 1) r = true;
        if (!r) return -1;
        lr.real = true;
        for (int a = 0; a < 8; ++a) lr.rest[a] = axis_val(s, a);
        return -1;
    }
    int ends = 0, moved = -1;
    for (int a = 0; a < 8; ++a) {
        if (a == skip || !(lr.present & (1u << a)) || !end_band(lr.rest[a])) continue;
        ++ends;
        if (moved < 0 && labs(axis_val(s, a) - lr.rest[a]) > 32767) moved = a;
    }
    return ends >= minEnds ? moved : -1;
}

// A gear shifter without "shift" in its name (a generic USB shifter) looks like a small button
// box: learn it from use. It has no axes and few buttons (a button box has many), and a gear
// stays held - one button down for over 3 s, which rim / box buttons never are. Named button
// boxes (ButtonBox1..3) are never taken. Feed every reading of an automatic slot.
static bool box_is_shifter(ExtraDev& x, unsigned raw, DWORD now)
{
    bool one = raw && !(raw & (raw - 1));
    if (!one || raw != x.prev) { x.heldSince = one ? now : 0; return false; }
    return x.heldSince && now - x.heldSince > 3000 && x.nbtn && x.nbtn <= 16 && !x.learn.present && !x.filter[0];
}

// Under g_wcs: slot k turned out to be a gear shifter - it becomes the shifter (H-pattern: a
// held gear is what gave it away).
static void adopt_shifter(int k)
{
    ExtraDev& s = g_x[X_SHIFTER];
    ExtraDev& x = g_x[k];
    s.dev = x.dev; s.coop = x.coop; s.acq = x.acq; s.prev = 0; s.id = x.id; s.nbtn = x.nbtn;
    lstrcpynA(s.name, x.name, sizeof s.name);
    s.learn = x.learn;
    x.dev = NULL;
    memset(&g_sh, 0, sizeof g_sh);
    g_sh.mode = g_shiftCfg == SH_AUTO ? SH_H : g_shiftCfg;
    shim_log("shifter: learned - '%s' holds a button like a gear: it is the shifter (%s)", s.name,
             g_sh.mode == SH_SEQ ? "sequential, as set" : "H-pattern");
    controls_summary();
}

// Under g_wcs: slot k turned out to hold the pedals - it becomes the pedal box.
static void adopt_pedals(int k, int ax)
{
    ExtraDev& x = g_x[k];
    g_pdev = x.dev; g_pCoopWnd = x.coop; g_pAcquired = x.acq;
    lstrcpynA(g_pname, x.name, sizeof g_pname);
    memset(&g_pAx, 0, sizeof g_pAx); g_pAx.present = x.learn.present;
    g_axPresent = x.learn.present;
    Learn lr = x.learn;
    x.dev = NULL;
    g_pedalLearn = false; g_pedalHow = "learned";
    shim_log("pedals: learned - '%s' axis '%s' moves like a pedal: gas and brake come from it", g_pname, ax_name(ax));
    pedals_reset();
    choose_layout(x.id, &lr, -1);
    controls_summary();
}

static void log_axes(AxLog& a, const DIJOYSTATE2& s, const char* who)
{
    if (!a.real) {                                // DirectInput shows mid-axis until the first report
        bool r = false;
        for (int i = 0; i < 8; ++i)
            if ((a.present & (1u << i)) && labs(axis_val(s, i) - 32767) > 1) r = true;
        if (!r) return;
        a.real = true;
    }
    for (int i = 0; i < 8; ++i) {
        long v = axis_val(s, i);
        if (!a.have) { a.start[i] = v; continue; }
        if (!a.logged[i] && labs(v - a.start[i]) > 13000) {
            a.logged[i] = true;
            shim_log("%s: axis '%s' moves (from %ld to %ld)", who, s_axName[i], a.start[i], v);
        }
    }
    a.have = true;
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
//
// Full = the deepest point this pedal has reached so far, but at least half of the way to the
// axis end - so 100 % arrives where the pedal really stops (a load-cell brake, a pedal set
// short in its own software) and a light first press does not count as full.
static int separate_pedal(PedalState& p, long raw, int ax, const char* what)
{
    if (raw < 0) return 0;
    if (p.rest < 0) { p.rest = raw; shim_log("wheel: %s pedal released at %ld (axis '%s')", what, raw, ax_name(ax)); }
    long down = p.rest >= 32768 ? 0 : 65535;   // the fully pressed end
    if (labs(raw - down) > labs(p.rest - down)) p.rest = raw;
    long span = labs(down - p.rest), travel = labs(raw - p.rest);
    if (p.which >= 1 && p.which <= 2 && g_calId) {   // the cabinet-style calibration
        PedalCal& c = g_cal[p.which];
        c.raw = span ? (int)(travel * 255 / span) : 0;
        if (c.max - c.min < 40 && c.max == 0 && c.min == 0) c.min = c.raw;   // fresh: start at the rest
        if (c.raw < c.min) { c.min = c.raw; g_calDirty = true; }
        if (c.raw > c.max) { c.max = c.raw; g_calDirty = true; }
        if (cal_valid(p.which)) {
            int v = pedal(c.raw - c.min, c.max - c.min);
            pedal_log(p, what, v, ax, raw, p.rest);
            return v;
        }
    }
    if (travel > p.travel) p.travel = travel;
    long full = p.travel > span / 2 ? p.travel : span / 2;
    int v = pedal(travel, full);
    pedal_log(p, what, v, ax, raw, p.rest);
    if (v == 0 && p.travel > span / 2 && p.travel - p.travelLogged > span / 20) {   // released: report a new full
        p.travelLogged = p.travel;
        shim_log("wheel: %s calibrated - full at %ld%% of the axis", what, span ? p.travel * 100 / span : 0);
    }
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
    if (g_woff || (!g_dev && !g_pdev)) return false;
    EnterCriticalSection(&g_wcs);
    *steer = *gas = *brake = 0; *buttons = 0;      // unfocused / no report yet: neutral, still "present"
    bool focus = pad_process_has_focus();
    DIJOYSTATE2 s, ps;
    HRESULT hr = DIERR_NOTACQUIRED, phr = DIERR_NOTACQUIRED;
    if (g_dev && focus && ensure_acquired()) {
        g_dev->Poll();
        hr = g_dev->GetDeviceState(sizeof s, &s);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) { g_acquired = false; ffb_release(); }
    }
    {   // what reading the wheel did, each time it changes (focus / acquire / read result)
        static long s_last = 0x7FFFFFFF;
        long st = !focus ? 1 : !g_acquired ? 2 : (long)hr;
        if (g_dev && st != s_last) {
            s_last = st;
            if (st == 1) shim_log("wheel: not read - another program's window is in front");
            else if (st == 2) shim_log("wheel: not read - could not acquire it");
            else shim_log("wheel: read -> hr 0x%08lX%s", (unsigned long)hr, SUCCEEDED(hr) ? " (ok)" : "");
        }
    }
    if (g_pdev && focus && ensure_shared_acquired(g_pdev, &g_pCoopWnd, &g_pAcquired)) {
        g_pdev->Poll();
        phr = g_pdev->GetDeviceState(sizeof ps, &ps);
        if (phr == DIERR_INPUTLOST || phr == DIERR_NOTACQUIRED) g_pAcquired = false;
    }
    if (g_testPad && g_dev && SUCCEEDED(hr) && g_testFed) {   // TEST: the triggers as the pad code reads them
        s.lRy = g_testGas * 257; s.lRx = g_testBrake * 257;
        s.lX = 32768 + g_testSteer * 258;          // -127..127 -> the axis, centre 32768
    }
    if (g_testPad && g_dev && SUCCEEDED(hr)) {    // TEST: what DirectInput delivers, when it changes
        static long s_rx = -2, s_ry = -2;
        static DWORD s_t = 0;
        DWORD t = GetTickCount();
        if ((labs(s.lRx - s_rx) > 2000 || labs(s.lRy - s_ry) > 2000) && t - s_t > 300) {
            s_t = t; s_rx = s.lRx; s_ry = s.lRy;
            shim_log("wheel: TEST rx %ld ry %ld (x %ld y %ld z %ld rz %ld)", s.lRx, s.lRy, s.lX, s.lY, s.lZ, s.lRz);
        }
    }
    // pedals: from the pedal box when there is one, else from the wheel
    if (g_pdev ? SUCCEEDED(phr) : SUCCEEDED(hr))
        read_pedals(g_pdev ? ps : s, gas, brake);
    if (g_pdev && SUCCEEDED(phr)) log_axes(g_pAx, ps, "pedals");
    if (g_dev && SUCCEEDED(hr)) {
        log_axes(g_wAx, s, "wheel");
        if (g_pedalLearn && !g_pdev) {            // a pedal on the wheel itself settles it
            int a = pedal_moved(g_wLearn, s, g_axSteer, 1);
            if (a >= 0) {
                g_pedalLearn = false;
                shim_log("pedals: the wheel's axis '%s' moves like a pedal - pedals stay on the wheel", ax_name(a));
                controls_summary();
            }
        }
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
        for (int i = 0; i < 32; ++i) if (s.rgbButtons[i] & 0x80) { raw |= 1u << i; if (!g_testPad) ub |= g_btnMap[i]; }
        for (int i = 0; i < 32; ++i)
            if ((raw & ~prevRaw) & (1u << i)) shim_log("wheel: button %d pressed", i);
        prevRaw = raw;
        DWORD pov = s.rgdwPOV[0];
        if (LOWORD(pov) != 0xFFFF) ub |= pad_hat_bits((int)((pov + 2250) / 4500) % 8);
        *buttons = ub;
        ffb_update();
    }
    for (int k = 0; k < X_COUNT; ++k) {            // shifter / button boxes, read with the wheel
        ExtraDev& x = g_x[k];
        if (!x.dev || FAILED(hr) || !ensure_shared_acquired(x.dev, &x.coop, &x.acq)) continue;
        DIJOYSTATE2 xs;
        x.dev->Poll();
        HRESULT xr = x.dev->GetDeviceState(sizeof xs, &xs);
        if (xr == DIERR_INPUTLOST || xr == DIERR_NOTACQUIRED) x.acq = false;
        if (SUCCEEDED(xr)) {
            if (k != X_SHIFTER && g_pedalLearn && !g_pdev) {   // pedals on this device?
                int a = pedal_moved(x.learn, xs, -1, 2);
                if (a >= 0) { adopt_pedals(k, a); continue; }
            }
            unsigned raw = 0;
            for (int i = 0; i < 32; ++i) if (xs.rgbButtons[i] & 0x80) raw |= 1u << i;
            if (k != X_SHIFTER && !g_x[X_SHIFTER].dev && box_is_shifter(x, raw, GetTickCount())) {
                adopt_shifter(k);
                continue;                          // read as the shifter from the next frame
            }
            if (k == X_SHIFTER) {
                int learned;
                *buttons |= shifter_bits(g_sh, raw, GetTickCount(), x.map, g_seqMap, &learned);
                if (learned != SH_AUTO) {
                    shim_log("shifter: used as %s (learned)", learned == SH_H ? "an H-pattern shifter" : "a sequential shifter");
                    controls_summary();
                }
            } else {
                for (int i = 0; i < 32; ++i) if (raw & (1u << i)) *buttons |= x.map[i];
            }
            for (int i = 0; i < 32; ++i)
                if ((raw & ~x.prev) & (1u << i)) shim_log("%s: button %d pressed", x_role[k], i);
            x.prev = raw;
            DWORD pov = xs.rgdwPOV[0];             // a button box's hat acts like the wheel's
            if (k != X_SHIFTER && LOWORD(pov) != 0xFFFF) *buttons |= pad_hat_bits((int)((pov + 2250) / 4500) % 8);
        } else if (xr == DIERR_UNPLUGGED || xr == E_HANDLE) {
            drop_extra(k, "unplugged");
        }
    }
    if (hr == DIERR_UNPLUGGED || hr == E_HANDLE) {
        drop_device("unplugged");
        for (int k = 0; k < X_COUNT; ++k) drop_extra(k, "wheel gone");
    }
    if (phr == DIERR_UNPLUGGED || phr == E_HANDLE) drop_pedals("unplugged");
    cal_save(GetTickCount());
    bool ok = g_dev != NULL || g_pdev != NULL;
    LeaveCriticalSection(&g_wcs);
    return ok;
}

// The cabinet I/O board's calibration for PC pedals (GvrIOShim.cpp): axis 1 gas, 2 brake ->
// raw position, min, max (0..255). False when there are no PC pedals with their own axes (a
// pad, combined Logitech pedals): the shim then answers as before.
bool wheel_cal_axis(int axis, int* raw, int* mn, int* mx)
{
    if (g_woff || axis < 1 || axis > 2) return false;
    EnterCriticalSection(&g_wcs);
    bool ok = (g_dev || g_pdev) && g_calId && !g_combined;
    if (ok) { *raw = g_cal[axis].raw; *mn = g_cal[axis].min; *mx = g_cal[axis].max; }
    LeaveCriticalSection(&g_wcs);
    return ok;
}

// "Calibrate the accelerator and brake" / Control Test (Start + Music): msg 0x53. Like the board:
// forget min / max - both start at the pedal's present position and grow as it moves.
bool wheel_cal_reset(void)
{
    if (g_woff) return false;
    EnterCriticalSection(&g_wcs);
    bool ok = (g_dev || g_pdev) && g_calId && !g_combined;
    if (ok) {
        for (int w = 1; w <= 2; ++w) g_cal[w].min = g_cal[w].max = g_cal[w].raw;
        g_calDirty = true;
    }
    LeaveCriticalSection(&g_wcs);
    if (ok) shim_log("pedals: calibration reset - press each pedal fully");
    return ok;
}

void wheel_attach(void)
{
    InitializeCriticalSection(&g_wcs);
    InitializeCriticalSection(&g_dcs);
}
