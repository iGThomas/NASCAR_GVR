// ============================================================================
//  GvrIOShim  ->  GvrIO.dll   (NASCAR Team Racing, GlobalVR)
//
//  Fixes the teardown deadlock that stops NASCAR_GVR.exe from starting.
//
//  THE BUG (recovered with Ghidra; GvrIO.dll's preferred base 0x10000000 is
//  also its live base, so these RVAs are absolute addresses in the OEM DLL):
//
//    cGvrIO::~cGvrIO  (RVA 0x2570)
//        1000257A  MOV byte [1000C02C],0     ; clear the heartbeat
//        10002581  PUSH 1
//        10002583  CALL ESI                  ; Sleep(1)
//        10002585  MOV AL,[1000C02C]
//        1000258A  TEST AL,AL
//        1000258C  JZ 10002581               ; spin until the worker acks
//
//  0x1000C02C is a per-iteration heartbeat that the worker thread created at
//  the end of cGvrIO::Initialize sets at RVA 0x2336, right before its own
//  Sleep. So the destructor means "wait until the worker has gone round once".
//  With no cabinet I/O hardware that worker never runs (verified by
//  enumerating every thread of the stalled game), so the game hangs during
//  startup at ~15 MB and 0% CPU - it never reaches graphics, or its dongle
//  check.
//
//  THE SHIM
//  Eight of the eleven exports are plain FORWARDERS to GvrIO_oem.dll (see
//  GvrIOShim.def), so that OEM code runs untouched. The destructor, Initialize
//  and GvrIOMessageSend are local. Four things are added:
//
//    1. Initialize is overridden: it calls the OEM Initialize and then reports
//       success regardless, so the game never decides to tear cGvrIO down.
//       (Letting the OEM teardown run on half-initialised state makes ntdll's
//       heap code spin at 100% CPU - observed.)
//    2. A watchdog thread supplies the missing heartbeat, so if anything does
//       destroy a cGvrIO the destructor cannot hang. It is deliberately a
//       watchdog and not a blanket override: it only writes the flag after it
//       has stayed 0 for ~120 ms, so a live worker always wins the race and
//       the genuine synchronisation is preserved. Blanket-stubbing cabinet
//       probes is what caused the long hangs in the NFSU project (see
//       shell-oem-abi-forwarding), and NOPping the JZ outright got the game to
//       Game::CreateManagers and then a heap access violation.
//    3. The HASP dongle gate in NASCAR_GVR.exe is stubbed in memory, so the
//       game starts with no cabinet hardware. See patch_dongle_gate below for
//       the gate's three checks and why 39 bytes satisfy all of them. This
//       shim is the hook because it is already mapped into the game before
//       main() runs, which leaves NASCAR_GVR.exe byte-identical on disk.
//    4. GvrIOMessageSend forwards every message to the OEM and only fills in
//       the cabinet I/O board's version (0x2F/0x30 -> 3.03) and the pedal axis
//       range (0x39/0x3A -> 0..255) when the OEM has no usable answer, so the
//       Anark shell's power-on checks pass on a PC with no cabinet board.
//       It also answers the live axis value (msg 5 = Gvr.AxisX/Y/Z) with centred
//       values when no analog device exists (decided from the gas axis), and lets
//       Left/D and Right/G steer while the process has focus - car select etc.
//       A real board/wheel always wins. GVRIOSHIM_NO_IOBOARD=1 disables it.
//       Deployed as the SHELL's GvrIO.dll too (Shell\bin, same OEM build).
//    5. Xbox / PlayStation controllers (GvrIOPad.cpp), in the front end AND the
//       race: the sticks and triggers answer msg 5 when no real wheel is fitted,
//       and the buttons are added to GvrIO's own keyboard state, so each one acts
//       as the cabinet key it stands for. Map in nascar_settings.ini [Controller]
//       / [Frontend]; GVRIOSHIM_NO_PAD=1 disables it.
//
//  The Initialize override is declared through the real C++ class shape so the
//  compiler emits the exact decorated name and __thiscall convention. Writing
//  the mangled name by hand in the .def does not work: the parser treats the
//  '@' characters as an ordinal separator and truncates the export to
//  "?Initialize", which makes the game fail to start with "the procedure entry
//  point ... could not be located".
//
//  Deploy: rename the OEM DLL to GvrIO_oem.dll, drop this in as GvrIO.dll.
//  For a trace next to the executable (gvrioshim.log), set nascar_settings.ini
//  [Debug] Log=true, or the environment variable GVRIOSHIM_LOG=1.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "GvrIOPad.h"

static const DWORD kHeartbeatRva      = 0x0000C02C;  // cGvrIO's worker-ack byte
static const int   kZeroMsBeforeWeAct = 120;         // let a real worker win first

static bool g_logging = false;
static char g_logPath[MAX_PATH] = { 0 };

static void vlog(const char* fmt, va_list ap)
{
    if (!g_logging) return;
    FILE* f = fopen(g_logPath, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    vfprintf(f, fmt, ap);
    fprintf(f, "\n");
    fclose(f);
}

static void logf(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

void shim_log(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

static HMODULE oem_module(void)
{
    HMODULE h = GetModuleHandleA("GvrIO_oem.dll");
    if (!h) h = LoadLibraryA("GvrIO_oem.dll");
    return h;
}

// ------------------------------------------------------- HASP dongle gate
//
// NASCAR_GVR.exe refuses to start without its Aladdin HASP dongle, and the
// refusal is silent: main() (FUN_0045dc80) keeps a local flag that is only
// cleared when all three of
//
//     FUN_006721d0()                          -> dongle present
//     FUN_006721f0()                          -> dongle valid
//     stricmp(FUN_00672220(), "NASCAR") == 0  -> dongle is for THIS game
//
// succeed. If any fails the entire INIT / RESTART / play sequence is skipped
// and the process just exits(-1) - no message, no log, no WER event.
//
// All three reduce to one function. FUN_00672460(idx) zeroes a 27-byte record
// at 0x00779CC0 + idx*0x1b, asks the statically linked HASP services to fill it
// (FUN_00672280), and on success sets record[0] = 1 and returns 1:
//
//     FUN_006721d0 / FUN_006721f0  ->  FUN_00672460(0)
//     FUN_006724c0(0)              ->  record[0]           (the present byte)
//     FUN_006724f0(0)              ->  &record[1]           (the game name)
//     FUN_00672220()               ->  FUN_006724c0(0) ? FUN_006724f0(0) : 0
//
// So one 39-byte stub over FUN_00672460 satisfies the whole gate: write 1 to
// record[0] and "NASCAR\0" to record[1..7], return 1. The real function is 83
// bytes (0x00672460..0x006724B2, then CC padding), so the stub fits with room
// to spare and nothing after it is disturbed.
//
// HASP is linked into the executable, so there is no DLL to replace. We patch
// in memory from here instead of editing NASCAR_GVR.exe, because this shim is
// already loaded into the game before main() runs and this keeps the OEM
// executable byte-identical on disk - whoever launches the game (the Anark
// shell, NascarLaunch.exe, or a debugger) gets the same behaviour.
//
// Guard rails: the original 16 bytes are verified before anything is written,
// the record address is computed from the module's actual load base rather than
// assuming 0x00400000, and GVRIOSHIM_NO_DONGLE_PATCH=1 skips the patch entirely
// so a cabinet with a genuine dongle can use the real check.

static const DWORD kDongleFnRva     = 0x00272460;   // FUN_00672460
static const DWORD kDongleRecordRva = 0x00379CC0;   // DAT_00779cc0

// First 16 bytes of the untouched FUN_00672460, as a refuse-to-patch signature:
//   mov edx,[esp+4] / test edx,edx / jl .. / cmp edx,[0x0077a290] / jg ..
static const BYTE kDongleFnSig[16] = {
    0x8B, 0x54, 0x24, 0x04, 0x85, 0xD2, 0x7C, 0x48,
    0x3B, 0x15, 0x90, 0xA2, 0x77, 0x00, 0x7F, 0x40
};

static void patch_dongle_gate(void)
{
    if (GetEnvironmentVariableA("GVRIOSHIM_NO_DONGLE_PATCH", NULL, 0) > 0) {
        logf("GVRIOSHIM_NO_DONGLE_PATCH set - leaving the HASP check alone");
        return;
    }

    HMODULE host = GetModuleHandleA(NULL);
    if (!host) { logf("dongle patch: no host module"); return; }

    BYTE* fn     = (BYTE*)host + kDongleFnRva;
    BYTE* record = (BYTE*)host + kDongleRecordRva;

    char exe[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exe, MAX_PATH);

    if (memcmp(fn, kDongleFnSig, sizeof(kDongleFnSig)) != 0) {
        // Either this is not NASCAR_GVR.exe or it is a different build. Either
        // way, writing 39 bytes of code over something we cannot identify is
        // exactly how you get an unexplainable crash, so don't.
        logf("dongle patch: signature mismatch at %p in '%s' - NOT patching", fn, exe);
        return;
    }

    BYTE stub[] = {
        0x8B, 0x44, 0x24, 0x04,                         // mov  eax,[esp+4]   ; idx
        0x85, 0xC0,                                     // test eax,eax
        0x75, 0x1C,                                     // jnz  fail          ; only index 0
        0xB8, 0x00, 0x00, 0x00, 0x00,                   // mov  eax,<record>  ; [9] patched below
        0xC6, 0x00, 0x01,                               // mov  byte [eax],1  ; present
        0xC7, 0x40, 0x01, 'N', 'A', 'S', 'C',           // mov  dword [eax+1],"NASC"
        0xC7, 0x40, 0x05, 'A', 'R', 0x00, 0x00,         // mov  dword [eax+5],"AR\0\0"
        0xB8, 0x01, 0x00, 0x00, 0x00,                   // mov  eax,1         ; success
        0xC3,                                           // ret
        0x33, 0xC0,                                     // fail: xor eax,eax
        0xC3                                            //       ret
    };
    *(DWORD*)(stub + 9) = (DWORD)(ULONG_PTR)record;

    DWORD old = 0;
    if (!VirtualProtect(fn, sizeof(stub), PAGE_EXECUTE_READWRITE, &old)) {
        logf("dongle patch: VirtualProtect failed (%lu)", GetLastError());
        return;
    }
    memcpy(fn, stub, sizeof(stub));
    VirtualProtect(fn, sizeof(stub), old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, sizeof(stub));

    logf("dongle gate stubbed: %u bytes at %p, record at %p (host base %p, '%s')",
         (unsigned)sizeof(stub), fn, record, host, exe);
}

// ------------------------------------------------- display-mode size limit
//
// The race's video manager constructor (0x507C46) sets the accepted display-mode range:
//     mov [edx+0x1F5C], 640      min width
//     mov [edx+0x1F60], 480      min height
//     mov [edx+0x1F64], 1600     max dimension   <-- 0x507C5A
// and the D3D mode enumeration (0x50841D) drops every mode wider or taller than that
// maximum (read only at 0x5084BC). On a 1920x1080 monitor 1920x1080 never enters the
// list, so "-width 1920 -height 1080" is rejected ("... resolution 1920 x 1080 passed on
// the command line is invalid!") for fullscreen AND windowed, and the race renders at a
// smaller mode that the window or monitor then stretches - visibly blurry.
// We raise the maximum to 4096 in memory before main() runs. The 10 instruction bytes
// are verified first; GVRIOSHIM_NO_MODE_PATCH=1 skips it. NASCAR_GVR.exe is not modified.
static const DWORD kModeMaxInsnRva = 0x00107C5A;   // 0x507C5A - base 0x400000
static const BYTE  kModeMaxInsn[10] = { 0xC7, 0x82, 0x64, 0x1F, 0x00, 0x00, 0x40, 0x06, 0x00, 0x00 };
static const DWORD kModeMaxNew      = 4096;

static void patch_mode_limit(void)
{
    if (GetEnvironmentVariableA("GVRIOSHIM_NO_MODE_PATCH", NULL, 0) > 0) {
        logf("GVRIOSHIM_NO_MODE_PATCH set - leaving the 1600-pixel mode limit alone");
        return;
    }
    HMODULE host = GetModuleHandleA(NULL);
    if (!host) return;
    BYTE* insn = (BYTE*)host + kModeMaxInsnRva;
    __try {
        if (memcmp(insn, kModeMaxInsn, sizeof(kModeMaxInsn)) != 0) {
            logf("mode-limit patch: bytes differ at %p - not this build / not the race, NOT patching", insn);
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("mode-limit patch: %p not readable in this process - skipped", insn);
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(insn + 6, 4, PAGE_EXECUTE_READWRITE, &old)) {
        logf("mode-limit patch: VirtualProtect failed (%lu)", GetLastError());
        return;
    }
    memcpy(insn + 6, &kModeMaxNew, 4);
    VirtualProtect(insn + 6, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), insn, sizeof(kModeMaxInsn));
    logf("mode-limit patch: max display-mode dimension 1600 -> %lu at %p", (unsigned long)kModeMaxNew, insn);
}

// ------------------------------------------------------ horn that stops
//
// The race's only working horn is the cabinet NOS input (GvrIO event onGvrNOSButtonDown/Up
// -> flag 0x91a66a; the .CTL "Control - Horn" makes no sound in this build). At 0x4DA348
// the race (re)starts vehicle sound 61 every frame while the flag is set, but the horn
// sample loops and nothing ever stops it - one press and it honked forever (flag watched
// live: it does go back to 0 on release). We route that spot through a stub that, once the
// flag is 0, stops sound 61 exactly the way the race stops its checkpoint warnings
// (0x482BD5: slot = owner+0xA80 or +0x10CC (byte owner+0xC8) + 0xE8 + id*0x14; if the
// handle is set, ebx/esi = handle pair, call 0x4A73F0, clear the active byte at
// owner + (id*9+0x34)*4). Original bytes verified first; GVRIOSHIM_NO_HORN_PATCH=1 skips.
static const DWORD kHornSite = 0x004DA348;
static const BYTE  kHornSiteBytes[10] = { 0x8A, 0x0D, 0x6A, 0xA6, 0x91, 0x00, 0x84, 0xC9, 0x74, 0x17 };

static __declspec(naked) void horn_stub(void)
{
    __asm {
        cmp  byte ptr ds:[0x0091A66A], 0
        jne  play
        cmp  byte ptr [eax + 0x964], 0      ; sound 61 not active -> nothing to stop
        je   done
        push ebx
        push esi
        push edi
        mov  edi, eax
        lea  edx, [edi + 0xA80]
        cmp  byte ptr [edi + 0xC8], 0
        jne  have_base
        lea  edx, [edi + 0x10CC]
    have_base:
        mov  ebx, dword ptr [edx + 0x5AC]
        test ebx, ebx
        je   restore
        mov  esi, dword ptr [edx + 0x5B0]
        mov  ecx, 0x004A73F0
        call ecx
        mov  byte ptr [edi + 0x964], 0
    restore:
        pop  edi
        pop  esi
        pop  ebx
    done:
        mov  ecx, 0x004DA369
        jmp  ecx
    play:                                   ; the original code
        push 0x3F800000
        push 0x3F800000
        push 0x3D
        push eax
        mov  eax, 1
        mov  ecx, 0x004F7710
        call ecx
        mov  ecx, 0x004DA369
        jmp  ecx
    }
}

static void patch_horn_stop(void)
{
    if (GetEnvironmentVariableA("GVRIOSHIM_NO_HORN_PATCH", NULL, 0) > 0) return;
    BYTE* site = (BYTE*)(ULONG_PTR)kHornSite;
    if ((BYTE*)GetModuleHandleA(NULL) != (BYTE*)0x00400000) return;
    __try {
        if (memcmp(site, kHornSiteBytes, sizeof kHornSiteBytes) != 0) {
            logf("horn patch: bytes differ at %p - not the race, NOT patching", site);
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    BYTE patch[10] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90 };
    *(DWORD*)(patch + 1) = (DWORD)(ULONG_PTR)horn_stub - (kHornSite + 5);
    DWORD old = 0;
    if (!VirtualProtect(site, sizeof patch, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(site, patch, sizeof patch);
    VirtualProtect(site, sizeof patch, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, sizeof patch);
    logf("horn patch: NOS horn now stops on release (%p)", site);
}

// The IO-board / pedal / steering answers below are for the SHELL (AMPlayer.exe) only;
// in the race (NASCAR_GVR.exe) every message passes through untouched, exactly as before.
static bool g_isShell = false;

// ---------------------------------------------------------------- watchdog

static DWORD WINAPI WatchdogThread(LPVOID)
{
    // The forwarded exports make the loader bind GvrIO_oem.dll for us, but that
    // can happen after DllMain, so wait for it instead of calling LoadLibrary
    // from inside DllMain (which is not safe).
    HMODULE oem = NULL;
    for (int i = 0; i < 200 && !oem; i++) {
        oem = GetModuleHandleA("GvrIO_oem.dll");
        if (!oem) Sleep(25);
    }
    if (!oem) oem = oem_module();
    if (!oem) { logf("GvrIO_oem.dll not found - watchdog inert"); return 0; }

    volatile BYTE* flag = (BYTE*)oem + kHeartbeatRva;
    logf("watching heartbeat at %p (GvrIO_oem base %p + 0x%X)", flag, oem, kHeartbeatRva);

    int zeroMs = 0;
    unsigned supplied = 0;
    for (;;) {
        if (*flag == 0) {
            if (++zeroMs >= kZeroMsBeforeWeAct) {
                *flag = 1;                 // no worker is coming; unblock teardown
                zeroMs = 0;
                if (++supplied <= 8 || (supplied % 100) == 0)
                    logf("heartbeat supplied (#%u) - nothing acked within %d ms",
                         supplied, kZeroMsBeforeWeAct);
            }
        }
        else zeroMs = 0;                   // a real worker is alive; stay out of it
        Sleep(1);
    }
}

// ------------------------------------------------- Initialize override
//
// Declared exactly as the OEM does, so the compiler produces
//   ?Initialize@cGvrIO@GvrIO@Zeus@Gvr@@QAE_NW4GVR_DEVICE_TYPES@234@PAUHWND__@@P6APAXPAUsMessage@234@@Z@Z
// with __thiscall. Verified against the OEM export table after building.

namespace Gvr { namespace Zeus { namespace GvrIO {

struct sMessage;
enum GVR_DEVICE_TYPES { GVR_DEVICE_TYPE_UNSET = 0 };
typedef void* (__cdecl *MessageSendFn)(sMessage*);

class cGvrIO {
public:
    __declspec(dllexport) bool Initialize(GVR_DEVICE_TYPES, struct HWND__*, MessageSendFn);
    __declspec(dllexport) ~cGvrIO();
};

// Did anything ever successfully initialise a cGvrIO in this process?
static volatile LONG g_initialized = 0;

bool cGvrIO::Initialize(GVR_DEVICE_TYPES devType, struct HWND__* hwnd, MessageSendFn cb)
{
    static const char* kOemInitialize =
        "?Initialize@cGvrIO@GvrIO@Zeus@Gvr@@QAE_NW4GVR_DEVICE_TYPES@234@PAUHWND__@@P6APAXPAUsMessage@234@@Z@Z";

    bool real = false;
    HMODULE oem = oem_module();
    if (!oem) logf("GvrIO_oem.dll missing - cannot forward Initialize");
    else {
        // The OEM Initialize creates GvrIO's DirectInput keyboard; hook it first so
        // controller buttons can act as cabinet keys (GvrIOPad.cpp).
        pad_hook_gvrio(oem);
        FARPROC raw = GetProcAddress(oem, kOemInitialize);
        if (!raw) logf("OEM Initialize export not found");
        else {
            // Call it as the member function it is: on MSVC x86 a pointer to a
            // non-virtual member of a single-inheritance class is one code
            // address, so this keeps __thiscall intact.
            typedef bool (cGvrIO::*PfnMember)(GVR_DEVICE_TYPES, struct HWND__*, MessageSendFn);
            union { FARPROC p; PfnMember m; } u;
            u.p = raw;
            __try {
                real = (this->*u.m)(devType, hwnd, cb);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                logf("OEM Initialize raised 0x%08X", GetExceptionCode());
            }
        }
    }
    if (real) InterlockedExchange(&g_initialized, 1);
    logf("Initialize(devType=%d, hwnd=%p) -> OEM said %s, reporting TRUE",
         (int)devType, hwnd, real ? "true" : "false");
    return true;   // never let the game decide to tear cGvrIO down
}

// ------------------------------------------------- destructor override
//
// The OEM destructor is only safe if cGvrIO was fully initialised:
//
//   * it waits for the worker thread's heartbeat, and that thread is created
//     at the END of Initialize;
//   * it calls DeleteCriticalSection on 0x1000C180, which the worker
//     initialises (InitializeCriticalSection at RVA 0x226B);
//   * it deletes six globals that Initialize populates.
//
// NASCAR_GVR.exe constructs a cGvrIO and destroys it again WITHOUT ever
// calling Initialize (confirmed: this shim's Initialize override is never
// reached, yet the destructor is). Running the OEM teardown in that state
// deletes an uninitialised critical section and frees pointers that were
// never allocated, which corrupts the heap - the game then dies with an
// access violation inside ntdll's heap code at Game::CreateManagers.
//
// So: if nothing was ever initialised, there is nothing to tear down. Return
// without touching any of it. The object is a handful of bytes and the process
// is starting up, so not freeing it costs nothing.
cGvrIO::~cGvrIO()
{
    if (!InterlockedCompareExchange(&g_initialized, 0, 0)) {
        logf("~cGvrIO on a never-initialised object - skipping the OEM teardown");
        return;
    }
    HMODULE oem = oem_module();
    FARPROC raw = oem ? GetProcAddress(oem, "??1cGvrIO@GvrIO@Zeus@Gvr@@QAE@XZ") : NULL;
    if (!raw) { logf("~cGvrIO: OEM destructor not found - skipping"); return; }
    typedef void (cGvrIO::*PfnDtor)();
    union { FARPROC p; PfnDtor m; } u;
    u.p = raw;
    logf("~cGvrIO forwarding to the OEM destructor (object was initialised)");
    __try {
        (this->*u.m)();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("OEM destructor raised 0x%08X", GetExceptionCode());
    }
}

}}} // namespace


// ------------------------------------------------ GvrIOMessageSend override
// The shell's startup checks (Shell.am) refuse to run without the cabinet's
// Nytric I/O board: IOBoardMajorVersion = message 0x2F, IOBoardMinorVersion =
// message 0x30 (ZeusIOPlugIn getters at 0x100023A0 / 0x100023E0), and require
// major >= 3 and minor >= 3 ("firmware 3.03 or higher"). With no board the OEM
// answer is empty, so the shell shows "ERROR DETECTED WITH GVRIO BOARD".
// We forward every message to the OEM and only fill in 0x2F/0x30 when the OEM
// has no (or a too-old) answer, so a real board always wins.
// GVRIOSHIM_NO_IOBOARD=1 disables the fallback.
namespace Gvr { namespace Zeus { namespace GvrIO {
    struct sMessage;
    __declspec(dllexport) void* GvrIOMessageSend(sMessage* msg);
} } }

// Does the OEM report a usable GAS-pedal range (msg 0x3A, axis 1, max >= 40)? "No analog
// device" is decided once for all three axes from the gas axis: an absent device has no
// usable pedal range, while the OEM's uncalibrated STEERING max happens to be >= 40 -
// which once let its garbage value through and kept the car selector scrolling.
typedef void* (__cdecl *OemSendFn)(Gvr::Zeus::GvrIO::sMessage*);
static bool real_pedals(OemSendFn oemFn, Gvr::Zeus::GvrIO::sMessage* msg)
{
    // same rule as the shell's calibration check: a usable gas range is max - min >= 40
    int copy[8]; memcpy(copy, msg, sizeof copy); copy[2] = 1;
    copy[1] = 0x39; void* lo = oemFn((Gvr::Zeus::GvrIO::sMessage*)copy);
    int mn = lo ? *(int*)lo : -1;
    copy[1] = 0x3A; void* hi = oemFn((Gvr::Zeus::GvrIO::sMessage*)copy);
    int mx = hi ? *(int*)hi : -1;
    return lo && hi && mn >= 0 && mx - mn >= 40;
}

void* Gvr::Zeus::GvrIO::GvrIOMessageSend(Gvr::Zeus::GvrIO::sMessage* msg)
{
    typedef void* (__cdecl *Fn)(sMessage*);
    static Fn oemFn = 0;
    static int s_board[2] = { 3, 3 };          // major, minor -> 3.03
    if (!oemFn) {
        HMODULE oem = oem_module();
        if (oem) oemFn = (Fn)GetProcAddress(oem, "?GvrIOMessageSend@GvrIO@Zeus@Gvr@@YAPAXPAUsMessage@123@@Z");
    }
    void* r = oemFn ? oemFn(msg) : 0;
    int id = msg ? ((int*)msg)[1] : -1;
    if (!g_isShell) {
        // The race: every message passes through untouched, except that a controller
        // drives the live axes (msg 5, device 1) when no real wheel and pedals are fitted.
        // The race reads them every frame (FUN_0045c650) and uses any nonzero value in
        // place of its keyboard input - see GvrIOPad.cpp.
        if (id == 5 && ((int*)msg)[0] == 1 && oemFn) {
            static int s_raceAxis[3];
            int axis = ((int*)msg)[2], v = 0;
            if (axis >= 0 && axis < 3 && pad_axis(axis, &v) && (!r || !real_pedals(oemFn, msg))) {
                s_raceAxis[axis] = v;
                r = &s_raceAxis[axis];
            }
        }
        return r;
    }
    if (id == 0x2F || id == 0x30) {
        int got = r ? *(int*)r : -1;
        bool fake = GetEnvironmentVariableA("GVRIOSHIM_NO_IOBOARD", NULL, 0) == 0 && got < 3;
        char b[160];
        _snprintf(b, sizeof b, "GvrIOShim: IO board msg 0x%X -> OEM %d%s\n", id, got, fake ? " -> reporting 3" : "");
        OutputDebugStringA(b); logf("%s", b);
        if (fake) r = &s_board[id == 0x2F ? 0 : 1];
    }
    // Pedal/steering calibration: Shell.am shows "Calibrate the accelerator and brake"
    // when AxisMaxY-AxisMinY < 40 or AxisMaxZ-AxisMinZ < 40. Axis min = msg 0x39, max =
    // msg 0x3A, axis index at +8 (0 X steering, 1 Y gas, 2 Z brake). With no analog
    // device nothing answers, so supply a full 0..255 calibrated range; a real device's
    // answer is passed through untouched.
    if ((id == 0x39 || id == 0x3A) && GetEnvironmentVariableA("GVRIOSHIM_NO_IOBOARD", NULL, 0) == 0) {
        static int s_min = 0, s_max = 255;
        int axis = ((int*)msg)[2];
        int got = r ? *(int*)r : -1;
        // Decide "uncalibrated" with the script's own rule: the OEM's max - min for this
        // axis is under 40. An absent device reports a max below 40; a stale calibration
        // (seen on a fresh install: a high max with an even higher min) passes a max-only
        // test and still fails the script's range check. Both ends are replaced together.
        int oemMin = -1, oemMax = -1;
        if (oemFn) {
            int copy[8]; memcpy(copy, msg, sizeof copy);
            copy[1] = 0x39; void* m = oemFn((sMessage*)copy); oemMin = m ? *(int*)m : -1;
            copy[1] = 0x3A; m = oemFn((sMessage*)copy);       oemMax = m ? *(int*)m : -1;
        }
        if (!r || oemMin < 0 || oemMax - oemMin < 40) {
            char b[160];
            _snprintf(b, sizeof b, "GvrIOShim: axis %d %s -> OEM range %d..%d -> reporting %d\n", axis, id == 0x39 ? "min" : "max", oemMin, oemMax, id == 0x39 ? s_min : s_max);
            OutputDebugStringA(b);
            static int logged = 0;
            if (logged++ < 12) logf("%s", b);
            r = (id == 0x39) ? &s_min : &s_max;
        }
    }
    // Live axis value: msg 5 on the analog device (+0 == 1), +8 = axis (0 X steering,
    // 1 Y gas, 2 Z brake) - ZeusIOPlugIn's Gvr.AxisX/Y/Z. With no wheel the OEM answer is
    // an uncalibrated value far past the shell's +/-20 dead zone, so NASCAR_Selection's car
    // selector scrolls forever (images never finish loading) until its timer picks a car.
    // When the OEM reports no usable range for the axis (max < 40, as above), answer from a
    // connected controller (stick, triggers, D-pad left/right) or else centred - and, while
    // a window of this process has focus, let the keyboard steer when the stick is centred:
    // Left / D = -60, Right / G = +60. A real wheel's answer is passed through untouched.
    if (id == 5 && ((int*)msg)[0] == 1 && GetEnvironmentVariableA("GVRIOSHIM_NO_IOBOARD", NULL, 0) == 0) {
        static int s_axis[3];
        static bool s_logged[3];
        int axis = ((int*)msg)[2];
        if (axis >= 0 && axis < 3 && oemFn) {
            if (!r || !real_pedals(oemFn, msg)) {
                int v = 0;
                bool pad = pad_axis(axis, &v);
                if (axis == 0 && v == 0 && pad_process_has_focus()) {
                    if ((GetAsyncKeyState(VK_LEFT) | GetAsyncKeyState('D')) & 0x8000)  v = -60;
                    if ((GetAsyncKeyState(VK_RIGHT) | GetAsyncKeyState('G')) & 0x8000) v = 60;
                }
                if (!s_logged[axis]) {
                    s_logged[axis] = true;
                    char b[160];
                    _snprintf(b, sizeof b, "GvrIOShim: axis %d value -> OEM %d -> reporting %s\n",
                              axis, r ? *(int*)r : -1, pad ? "the controller" : "centred/keyboard");
                    OutputDebugStringA(b);
                }
                s_axis[axis] = v;
                r = &s_axis[axis];
            }
        }
    }
    return r;
}

// ---------------------------------------------------------------- DllMain

// The trace is switched on by nascar_settings.ini [Debug] Log=true, or by the environment
// variable GVRIOSHIM_LOG=1 (for a run without an ini).
static bool log_requested(void)
{
    if (GetEnvironmentVariableA("GVRIOSHIM_LOG", NULL, 0) > 0) return true;
    char ini[MAX_PATH], v[32] = { 0 };
    if (!shim_settings_path(ini, sizeof ini)) return false;
    GetPrivateProfileStringA("Debug", "Log", "", v, sizeof v, ini);
    char* c = strpbrk(v, ";#");
    if (c) *c = 0;
    char* e = v + strlen(v);
    while (e > v && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return _stricmp(v, "true") == 0 || _stricmp(v, "1") == 0 || _stricmp(v, "yes") == 0 ||
           _stricmp(v, "on") == 0;
}

// ---------------------------------------------------------------- crash capture
// The shell (AMPlayer.exe) has no native crash log of its own; the race has the engine's own
// BADSTUFF filter into trace00N.txt but our DllMain runs first, so this is a useful backstop in
// both. We only LOG the faulting module+offset, then chain to whatever filter was there before
// (the game installs its own during main(), WER otherwise), so crash behaviour is unchanged.
// Installed only when logging is on, to keep a normal play session untouched.
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = NULL;

static LONG WINAPI crash_filter(EXCEPTION_POINTERS* ep)
{
    __try {
        EXCEPTION_RECORD* er = ep ? ep->ExceptionRecord : NULL;
        void* addr = er ? er->ExceptionAddress : NULL;
        unsigned long code = er ? er->ExceptionCode : 0;
        char mod[MAX_PATH] = "?"; unsigned long off = 0;
        HMODULE hm = NULL;
        if (addr && GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)addr, &hm) && hm) {
            GetModuleFileNameA(hm, mod, MAX_PATH);
            off = (unsigned long)((DWORD_PTR)addr - (DWORD_PTR)hm);
        }
        const char* b = strrchr(mod, '\\'); b = b ? b + 1 : mod;
        logf("*** UNHANDLED EXCEPTION code=0x%08lX addr=%p  %s+0x%lX ***", code, addr, b, off);
        if (er && code == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
            logf("    access violation %s address %p",
                 er->ExceptionInformation[0] ? "writing" : "reading",
                 (void*)er->ExceptionInformation[1]);
        CONTEXT* c = ep ? ep->ContextRecord : NULL;
        if (c) logf("    EIP=%08lX ESP=%08lX EBP=%08lX EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX",
                    (unsigned long)c->Eip, (unsigned long)c->Esp, (unsigned long)c->Ebp,
                    (unsigned long)c->Eax, (unsigned long)c->Ebx, (unsigned long)c->Ecx,
                    (unsigned long)c->Edx);
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        {
            char exe[MAX_PATH] = { 0 };
            GetModuleFileNameA(NULL, exe, MAX_PATH);
            const char* b = strrchr(exe, '\\');
            g_isShell = _stricmp(b ? b + 1 : exe, "AMPlayer.exe") == 0;
        }
        pad_attach(hInst, g_isShell);          // first: it also locates nascar_settings.ini
        if (log_requested()) {
            g_logging = true;
            const char* proc = g_isShell ? "AMPlayer" : "NASCAR_GVR";
            // Prefer the install-root LOG folder (shared with the launcher and the SQLite
            // provider); fall back to next to the executable if the root can't be found.
            char ini[MAX_PATH]; bool placed = false;
            if (shim_settings_path(ini, sizeof ini)) {
                char* s = strrchr(ini, '\\');
                if (s) {
                    *s = 0;                                    // <install root>
                    char logdir[MAX_PATH];
                    _snprintf(logdir, sizeof logdir, "%s\\LOG", ini); logdir[sizeof logdir - 1] = 0;
                    CreateDirectoryA(logdir, NULL);
                    _snprintf(g_logPath, sizeof g_logPath, "%s\\gvrioshim-%s-%lu.log",
                              logdir, proc, GetCurrentProcessId());
                    g_logPath[sizeof g_logPath - 1] = 0;
                    placed = true;
                }
            }
            if (!placed) {
                GetModuleFileNameA(NULL, g_logPath, MAX_PATH);
                char* slash = strrchr(g_logPath, '\\');
                if (slash) { slash[1] = 0; strcat(g_logPath, "gvrioshim.log"); }
                else strcpy(g_logPath, "gvrioshim.log");
            }
            logf("--- GvrIO shim attached (proc=%s pid=%lu) ---", proc, GetCurrentProcessId());
            g_prevFilter = SetUnhandledExceptionFilter(crash_filter);
        }
        // Before anything in this process reads the registry: the race imports this DLL,
        // and NascarLaunch loads it into the front end before AMPlayer's first instruction.
        reg_attach();
        // Before the game's main() runs, so the gate sees a satisfied record and the
        // video manager is constructed with the raised mode limit.
        patch_dongle_gate();
        patch_mode_limit();
        if (!g_isShell) patch_horn_stop();
        HANDLE h = CreateThread(NULL, 0, WatchdogThread, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
