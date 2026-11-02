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
//       probes is known to cause long, silent hangs, and NOPping the JZ
//       outright got the game to
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
#include "GvrIOWheel.h"

static const DWORD kHeartbeatRva      = 0x0000C02C;  // cGvrIO's worker-ack byte
static volatile DWORD g_watchdogTid = 0;             // its expected AV at exit is not logged (veh_handler)
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

// ---- locate a target by signature, so a build at a different address still patches --------
// The v1.1 fast paths below are unchanged; these scanners are only the fallback for other
// builds (NASCAR Team Racing 2.0.2 - gate 0x675B10, mode 0x5079B0 - nascar_gvr_old, and other
// disc pressings), so a shifted address can no longer silently skip a patch.
static BYTE*  g_textBase;
static SIZE_T g_textSize;
static void find_text_section(void)
{
    if (g_textBase) return;
    HMODULE host = GetModuleHandleA(NULL); if (!host) return;
    BYTE* base = (BYTE*)host;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (memcmp(sec[i].Name, ".text", 5) == 0) {
            g_textBase = base + sec[i].VirtualAddress;
            g_textSize = sec[i].Misc.VirtualSize;
            return;
        }
}
// Find pat[] in .text. mask==NULL means every byte matters; otherwise a 0 mask byte is a wildcard.
static BYTE* find_pattern(const BYTE* pat, const BYTE* mask, SIZE_T len)
{
    find_text_section();
    if (!g_textBase || g_textSize < len) return NULL;
    for (SIZE_T o = 0; o + len <= g_textSize; o++) {
        BYTE* p = g_textBase + o;
        SIZE_T k = 0;
        for (; k < len; k++) {
            if (mask && !mask[k]) continue;     // wildcard
            if (p[k] != pat[k]) break;
        }
        if (k == len) return p;
    }
    return NULL;
}

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
    bool v202 = false;   // set on the signature-scanned (non-v1.1) path

    if (memcmp(fn, kDongleFnSig, sizeof(kDongleFnSig)) != 0) {
        // Not v1.1 at the known address. Find the gate by signature so other builds still
        // patch (NASCAR Team Racing 2.0.2 etc.). The compared bound address (bytes 10..13)
        // is wildcarded; the shape is confirmed by imul eax,eax,0x1B at +0x12 and
        // lea esi,[eax+<record>] at +0x18, and the record address is read from that lea's
        // disp32 - so no per-build constant is needed. If it is genuinely absent this is not
        // the race (e.g. the shim is in the shell), and writing 39 bytes over an unidentified
        // address is how you get an unexplainable crash, so we do nothing.
        static const BYTE sig[16]  = { 0x8B,0x54,0x24,0x04, 0x85,0xD2, 0x7C,0x48, 0x3B,0x15, 0,0,0,0, 0x7F,0x40 };
        static const BYTE mask[16] = { 1,1,1,1, 1,1, 1,1, 1,1, 0,0,0,0, 1,1 };
        BYTE* hit = find_pattern(sig, mask, sizeof(sig));
        if (!hit || !(hit[0x12] == 0x6B && hit[0x13] == 0xC0 && hit[0x14] == 0x1B &&
                      hit[0x18] == 0x8D && hit[0x19] == 0xB0)) {
            logf("dongle patch: gate not found (not the race, or an unknown build) in '%s' - NOT patching", exe);
            return;
        }
        fn     = hit;
        record = (BYTE*)(ULONG_PTR)(*(DWORD*)(hit + 0x1A));   // the lea's disp32 = record VA
        v202   = true;
        logf("dongle patch: located gate by signature at %p, record %p (build != v1.1)", fn, record);
    }

    // Build the stub. Every build gets present=1 and game="NASCAR". NASCAR Team Racing 2.0.2's
    // race-gate (cleartext validator at 0x45F498) ALSO strcmp's the VERSION field (record+8)
    // against "2.0" and skips INIT/RESTART/DYN (the race) -> POST unless it matches. Record
    // layout, confirmed by static RE: +0 present, +1 game, +8 version, +0xC region, +0x19 cab;
    // only game and version are hard-gated. v1.1 does not hard-check the version, so the version
    // write is added ONLY on the signature-scanned path, keeping the shipped v1.1 stub identical.
    BYTE stub[64]; int n = 0;
    stub[n++]=0x8B; stub[n++]=0x44; stub[n++]=0x24; stub[n++]=0x04;                 // mov eax,[esp+4] ; idx
    stub[n++]=0x85; stub[n++]=0xC0;                                                 // test eax,eax
    int jnz=n; stub[n++]=0x75; stub[n++]=0x00;                                      // jnz fail (filled below)
    stub[n++]=0xB8; *(DWORD*)(stub+n)=(DWORD)(ULONG_PTR)record; n+=4;               // mov eax,<record>
    stub[n++]=0xC6; stub[n++]=0x00; stub[n++]=0x01;                                 // mov byte[eax],1 ; present
    stub[n++]=0xC7; stub[n++]=0x40; stub[n++]=0x01; stub[n++]='N'; stub[n++]='A'; stub[n++]='S'; stub[n++]='C'; // [eax+1]="NASC"
    stub[n++]=0xC7; stub[n++]=0x40; stub[n++]=0x05; stub[n++]='A'; stub[n++]='R'; stub[n++]=0x00; stub[n++]=0x00; // [eax+5]="AR\0\0"
    if (v202) { stub[n++]=0xC7; stub[n++]=0x40; stub[n++]=0x08; stub[n++]='2'; stub[n++]='.'; stub[n++]='0'; stub[n++]=0x00; } // [eax+8]="2.0\0"
    stub[n++]=0xB8; stub[n++]=0x01; stub[n++]=0x00; stub[n++]=0x00; stub[n++]=0x00; // mov eax,1 ; success
    stub[n++]=0xC3;                                                                 // ret
    int fail=n; stub[n++]=0x33; stub[n++]=0xC0; stub[n++]=0xC3;                     // fail: xor eax,eax ; ret
    stub[jnz+1] = (BYTE)(fail - (jnz+2));                                           // resolve jnz -> fail

    DWORD old = 0;
    if (!VirtualProtect(fn, n, PAGE_EXECUTE_READWRITE, &old)) {
        logf("dongle patch: VirtualProtect failed (%lu)", GetLastError());
        return;
    }
    memcpy(fn, stub, n);
    VirtualProtect(fn, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, n);

    logf("dongle gate stubbed: %d bytes at %p, record at %p%s (host base %p, '%s')",
         n, fn, record, v202 ? ", version=\"2.0\"" : "", host, exe);
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
// We raise the maximum to 16384 in memory before main() runs (4096 was too small for 5120x1440). The 10 instruction bytes
// are verified first; GVRIOSHIM_NO_MODE_PATCH=1 skips it. NASCAR_GVR.exe is not modified.
static const DWORD kModeMaxInsnRva = 0x00107C5A;   // 0x507C5A - base 0x400000
static const BYTE  kModeMaxInsn[10] = { 0xC7, 0x82, 0x64, 0x1F, 0x00, 0x00, 0x40, 0x06, 0x00, 0x00 };
static const DWORD kModeMaxNew      = 16384;

static void patch_mode_limit(void)
{
    if (GetEnvironmentVariableA("GVRIOSHIM_NO_MODE_PATCH", NULL, 0) > 0) {
        logf("GVRIOSHIM_NO_MODE_PATCH set - leaving the 1600-pixel mode limit alone");
        return;
    }
    HMODULE host = GetModuleHandleA(NULL);
    if (!host) return;
    BYTE* insn = (BYTE*)host + kModeMaxInsnRva;   // v1.1 fast path
    __try {
        if (memcmp(insn, kModeMaxInsn, sizeof(kModeMaxInsn)) != 0) {
            // Not v1.1 at the known address: the same 10-byte instruction exists in 2.0.2,
            // just relocated, so find it by signature. Absent -> not the race.
            insn = find_pattern(kModeMaxInsn, NULL, sizeof(kModeMaxInsn));
            if (!insn) {
                logf("mode-limit patch: instruction not found - not this build / not the race, NOT patching");
                return;
            }
            logf("mode-limit patch: located by signature at %p (build != v1.1)", insn);
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

// 2.0.2 shell: the cabinets ran US Windows, and the shell's .NET 1.1 code parses the
// database's US-format dates with the PC's own format: on a day/month Windows (nl-BE, de-DE,
// fr-FR, en-GB, ...) SafeConvert.ParseDate('12/31/1999 12:00:00 AM') throws a FormatException
// inside NASCAR_INITIALIZE_SHELL, the script engine's stack is corrupted (AMPlayer+0x28BE89)
// and the shell crashes ~30 s after start (reproduced under nl-BE; fine under en-US / en-BE,
// which .NET 1.1 does not know and treats as US). So this process gets the US locale: the
// locale queries the CLR makes (it imports exactly these) answer 0x0409. The Windows setting
// itself is untouched. GVRIOSHIM_NO_LOCALE_FIX=1 skips it (developer override).
static int WINAPI us_GetLocaleInfoW(LCID lcid, LCTYPE type, LPWSTR buf, int cch)
{
    WCHAR name[LOCALE_NAME_MAX_LENGTH] = L"en-US";
    if (lcid == LOCALE_USER_DEFAULT || lcid == LOCALE_SYSTEM_DEFAULT || lcid == 0x0409 || lcid == 0)
        type |= LOCALE_NOUSEROVERRIDE;
    else if (!LCIDToLocaleName(lcid, name, LOCALE_NAME_MAX_LENGTH, 0))
        lstrcpyW(name, L"en-US");
    return GetLocaleInfoEx(name, type, buf, cch);
}

static int WINAPI us_GetLocaleInfoA(LCID lcid, LCTYPE type, LPSTR buf, int cch)
{
    WCHAR w[256];
    int n = us_GetLocaleInfoW(lcid, type, w, 256);
    if (n <= 0) return n;
    if (type & LOCALE_RETURN_NUMBER) {                 // a DWORD, not text
        if (cch == 0) return sizeof(DWORD);
        if (cch < (int)sizeof(DWORD)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
        memcpy(buf, w, sizeof(DWORD));
        return sizeof(DWORD);
    }
    return WideCharToMultiByte(CP_ACP, 0, w, n, cch ? buf : NULL, cch, NULL, NULL);
}

static void patch_us_locale(void)
{
    if (GetEnvironmentVariableA("GVRIOSHIM_NO_LOCALE_FIX", NULL, 0) > 0) return;
    const BYTE* host = (const BYTE*)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(host + ((const IMAGE_DOS_HEADER*)host)->e_lfanew);
    DWORD stamp = nt->FileHeader.TimeDateStamp;                  // 2.0.2 / 1.1 AMPlayer.exe (both affected,
    if (stamp != 0x47C34847 && stamp != 0x46AFA277) return;      //   1.1 floods and never shows the attract)
    LCID before = GetUserDefaultLCID();
    static const char* const kFns[] = { "GetUserDefaultLCID", "GetThreadLocale", "GetSystemDefaultLCID",
                                        "GetUserDefaultLangID", "GetSystemDefaultLangID" };
    static const BYTE kStub[6] = { 0xB8, 0x09, 0x04, 0x00, 0x00, 0xC3 };   // mov eax, 0x409 ; ret
    HMODULE mods[2] = { GetModuleHandleA("kernel32.dll"), GetModuleHandleA("kernelbase.dll") };
    BYTE* done[16]; int nd = 0, patched = 0;
    for (int m = 0; m < 2; ++m) {
        if (!mods[m]) continue;
        for (int i = 0; i < 5; ++i) {
            BYTE* f = (BYTE*)GetProcAddress(mods[m], kFns[i]);
            bool seen = false;
            for (int j = 0; j < nd; ++j) if (done[j] == f) seen = true;
            if (!f || seen) continue;
            DWORD old = 0;
            if (!VirtualProtect(f, sizeof kStub, PAGE_EXECUTE_READWRITE, &old)) continue;
            memcpy(f, kStub, sizeof kStub);
            VirtualProtect(f, sizeof kStub, old, &old);
            FlushInstructionCache(GetCurrentProcess(), f, sizeof kStub);
            done[nd++] = f; ++patched;
        }
    }
    // The LCID alone is not enough (tested): the CLR then reads the culture's formats through
    // GetLocaleInfo for the current user, i.e. the user's own overrides (d/MM/yyyy). So
    // GetLocaleInfoW/A are answered by GetLocaleInfoEx: the user / system / en-US locale as
    // plain en-US without user overrides, any other locale by its own name.
    int formats = 0;
    for (int m = 0; m < 2; ++m) {
        if (!mods[m]) continue;
        const char* names[2] = { "GetLocaleInfoW", "GetLocaleInfoA" };
        void* hooks[2] = { (void*)us_GetLocaleInfoW, (void*)us_GetLocaleInfoA };
        for (int i = 0; i < 2; ++i) {
            BYTE* f = (BYTE*)GetProcAddress(mods[m], names[i]);
            bool seen = false;
            for (int j = 0; j < nd; ++j) if (done[j] == f) seen = true;
            if (!f || seen || nd >= 16) continue;
            BYTE jmp[5] = { 0xE9 };
            *(DWORD*)(jmp + 1) = (DWORD)(ULONG_PTR)hooks[i] - ((DWORD)(ULONG_PTR)f + 5);
            DWORD old = 0;
            if (!VirtualProtect(f, sizeof jmp, PAGE_EXECUTE_READWRITE, &old)) continue;
            memcpy(f, jmp, sizeof jmp);
            VirtualProtect(f, sizeof jmp, old, &old);
            FlushInstructionCache(GetCurrentProcess(), f, sizeof jmp);
            done[nd++] = f; ++formats;
        }
    }
    logf("locale: shell runs as en-US (Windows user locale 0x%04lX; %d id + %d format entry points)",
         (unsigned long)before, patched, formats);
}

// The cabinet software restarts Windows (nightly maintenance restart, operator "restart"
// screens). On a player's PC the game must never do that: the restart / shutdown entry points
// refuse inside the game's processes (BOOL ones return FALSE, InitiateShutdown returns
// ERROR_ACCESS_DENIED). The shell's own shutdown.exe launch is blocked in GvrIOPad.cpp.
static void block_pc_restart(void)
{
    struct Fn { const char* dll; const char* name; BYTE stub[8]; int len; };
    static const Fn kFns[] = {
        { "user32.dll",   "ExitWindowsEx",               { 0x31,0xC0,0xC2,0x08,0x00 }, 5 },             // xor eax,eax; ret 8
        { "advapi32.dll", "InitiateSystemShutdownA",     { 0x31,0xC0,0xC2,0x14,0x00 }, 5 },             // ret 0x14
        { "advapi32.dll", "InitiateSystemShutdownW",     { 0x31,0xC0,0xC2,0x14,0x00 }, 5 },
        { "advapi32.dll", "InitiateSystemShutdownExA",   { 0x31,0xC0,0xC2,0x18,0x00 }, 5 },             // ret 0x18
        { "advapi32.dll", "InitiateSystemShutdownExW",   { 0x31,0xC0,0xC2,0x18,0x00 }, 5 },
        { "advapi32.dll", "InitiateShutdownA",           { 0xB8,0x05,0x00,0x00,0x00,0xC2,0x14,0x00 }, 8 }, // mov eax,5; ret 0x14
        { "advapi32.dll", "InitiateShutdownW",           { 0xB8,0x05,0x00,0x00,0x00,0xC2,0x14,0x00 }, 8 },
    };
    int n = 0;
    for (int i = 0; i < (int)(sizeof kFns / sizeof kFns[0]); ++i) {
        HMODULE m = GetModuleHandleA(kFns[i].dll);    // this DLL links both, so they are mapped (no LoadLibrary in DllMain)
        BYTE* f = m ? (BYTE*)GetProcAddress(m, kFns[i].name) : NULL;
        if (!f) continue;
        DWORD old = 0;
        if (!VirtualProtect(f, kFns[i].len, PAGE_EXECUTE_READWRITE, &old)) continue;
        memcpy(f, kFns[i].stub, kFns[i].len);
        VirtualProtect(f, kFns[i].len, old, &old);
        FlushInstructionCache(GetCurrentProcess(), f, kFns[i].len);
        ++n;
    }
    logf("restart guard: %d Windows restart/shutdown entry points refuse in this process", n);
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

// 2.0.2 race (NASCAR_GVR.exe, link stamp 0x4C7EC23F): its music manager (0x4AACD0) turns a
// Music press into a volume step (Loud -> Soft -> Off, [this+0xB0]); 1.1's Music button is
// "next song". nascar_settings.ini [Race] MusicButton = next (default) makes 2.0.2 do the
// same: the press keeps its 20-frame cool-down but jumps into the manager's own "song ended"
// path (0x4AADAA: playlist index + 1, wrap, start that song), instead of the volume step.
// MusicButton = volume keeps the arcade behaviour. Applied in memory; the exe is untouched.
static const DWORD kMusicSite = 0x004AAD16, kMusicNext = 0x004AADAA;
static const BYTE  kMusicSiteBytes[] = { 0x8B,0x96,0xB0,0x00,0x00,0x00,0x42,0x8B,0xC2,0x83,0xF8,0x03,
                                         0x88,0x9E,0xB8,0x00,0x00,0x00,0xC7,0x86,0xAC,0x00,0x00,0x00 };
static const BYTE  kMusicNextBytes[] = { 0x8B,0x86,0x8C,0x00,0x00,0x00,0x3B,0xC3,0x7E,0x2E };

static void patch_music_button(void)
{
    char ini[MAX_PATH], v[32] = "next";
    if (shim_settings_path(ini, sizeof ini)) GetPrivateProfileStringA("Race", "MusicButton", "next", v, sizeof v, ini);
    char* c = strpbrk(v, " \t;#"); if (c) *c = 0;
    if (_stricmp(v, "volume") == 0) { logf("music button: volume (as on the cabinet)"); return; }
    BYTE* site = (BYTE*)(ULONG_PTR)kMusicSite;
    if ((BYTE*)GetModuleHandleA(NULL) != (BYTE*)0x00400000) return;
    __try {
        if (memcmp(site, kMusicSiteBytes, sizeof kMusicSiteBytes) != 0 ||
            memcmp((BYTE*)(ULONG_PTR)kMusicNext, kMusicNextBytes, sizeof kMusicNextBytes) != 0)
            return;                                        // not the 2.0.2 race (1.1 already skips)
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    BYTE patch[21] = {
        0x88,0x9E,0xB8,0x00,0x00,0x00,                     // mov byte [esi+0xB8], bl   (press handled)
        0xC7,0x86,0xAC,0x00,0x00,0x00,0x14,0x00,0x00,0x00, // mov dword [esi+0xAC], 20  (cool-down)
        0xE9,0,0,0,0 };                                    // jmp 0x4AADAA              (next song)
    *(DWORD*)(patch + 17) = kMusicNext - (kMusicSite + sizeof patch);
    DWORD old = 0;
    if (!VirtualProtect(site, sizeof patch, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(site, patch, sizeof patch);
    VirtualProtect(site, sizeof patch, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, sizeof patch);
    logf("music button: next song (%p)", site);
}

// 2.0.2 race playlist: the song table at 0x76B674 holds 2 entries {name, file size, flags, -1}
// (Le Mans, Meathook); the playlist builder (0x4AA780) walks it up to 0x76B694 and keeps a song
// only if Audio\Music\<name> exists with exactly that size, and "start song" (0x4AAA17) caps the
// index at 2. The disc also ships Tiger Fists (unused) and 1.1 had Chow Time, so with
// nascar_settings.ini [Race] MusicPlaylist = all (default) the table is replaced by a 4-entry one
// here, with each size read from the file on disk (a missing file is simply skipped by the
// game's own check). MusicPlaylist = original keeps the 2 songs. In memory only.
struct SongEntry { const char* name; DWORD size; BYTE flags[4]; DWORD tail; };
static SongEntry g_songs[4];
static const char* const kSongNames[4] = { "LE MANS.DJS", "MEATHOOK.DJS", "TIGER FISTS.DJS", "CHOW_TIME.DJS" };

static void patch_music_playlist(void)
{
    char ini[MAX_PATH], v[32] = "all";
    if (shim_settings_path(ini, sizeof ini)) GetPrivateProfileStringA("Race", "MusicPlaylist", "all", v, sizeof v, ini);
    char* c = strpbrk(v, " \t;#"); if (c) *c = 0;
    if (_stricmp(v, "original") == 0) { logf("music playlist: original 2 songs"); return; }
    if ((BYTE*)GetModuleHandleA(NULL) != (BYTE*)0x00400000) return;
    // {address of the instruction, its bytes before the immediate, the immediate}
    struct Site { DWORD at; BYTE op[2]; int oplen; DWORD imm; };
    static const Site kSites[] = {
        { 0x004AA788, { 0xBD },       1, 0x0076B674 },  // mov ebp, table
        { 0x004AA7A2, { 0x81, 0xFD }, 2, 0x0076B674 },  // cmp ebp, table
        { 0x004AA7AA, { 0x81, 0xFD }, 2, 0x0076B694 },  // cmp ebp, table end
        { 0x004AA852, { 0x3D },       1, 0x0076B694 },  // cmp eax, table end
        { 0x004AAA1F, { 0x05 },       1, 0x0076B674 },  // add eax, table
    };
    static const BYTE kCap[] = { 0x83, 0xF8, 0x02 };    // 0x4AAA17 cmp eax, 2
    __try {
        for (int i = 0; i < 5; ++i) {
            const BYTE* p = (const BYTE*)(ULONG_PTR)kSites[i].at;
            if (memcmp(p, kSites[i].op, kSites[i].oplen) != 0 || *(const DWORD*)(p + kSites[i].oplen) != kSites[i].imm)
                return;                                    // not the 2.0.2 race
        }
        if (memcmp((const void*)(ULONG_PTR)0x004AAA17, kCap, sizeof kCap) != 0) return;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\'); if (slash) slash[1] = 0;
    // Only songs that exist go in: the game numbers its playlist 0..count-1 over the songs that
    // passed its check and then uses those numbers as table positions, so a gap would shift them.
    int found = 0;
    for (int i = 0; i < 4; ++i) {
        char path[MAX_PATH];
        _snprintf(path, sizeof path, "%sAudio\\Music\\%s", dir, kSongNames[i]); path[sizeof path - 1] = 0;
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) continue;
        g_songs[found].name = kSongNames[i];
        g_songs[found].size = fa.nFileSizeLow;
        g_songs[found].tail = 0xFFFFFFFF;
        ++found;
    }
    if (found < 2) { logf("music playlist: only %d song file(s) found - keeping the original table", found); return; }
    DWORD table = (DWORD)(ULONG_PTR)g_songs, end = table + found * sizeof(SongEntry);
    for (int i = 0; i < 5; ++i) {
        BYTE* imm = (BYTE*)(ULONG_PTR)kSites[i].at + kSites[i].oplen;
        DWORD val = kSites[i].imm == 0x0076B674 ? table : end, old = 0;
        if (!VirtualProtect(imm, 4, PAGE_EXECUTE_READWRITE, &old)) return;
        *(DWORD*)imm = val;
        VirtualProtect(imm, 4, old, &old);
    }
    BYTE* cap = (BYTE*)(ULONG_PTR)0x004AAA19; DWORD old = 0;
    if (VirtualProtect(cap, 1, PAGE_EXECUTE_READWRITE, &old)) { *cap = (BYTE)found; VirtualProtect(cap, 1, old, &old); }
    FlushInstructionCache(GetCurrentProcess(), (void*)(ULONG_PTR)0x004AA780, 0x2A0);
    logf("music playlist: %d of 4 songs found (Le Mans, Meathook, Tiger Fists, Chow Time)", found);
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

    // One-shot: report which CLR the managed shell actually bound. 2.0.2's PLUSDE is a 1.1
    // mixed-mode assembly; under CLR 2.0/4.0 its native C++ exception interop access-violates.
    // mscorwks.dll = CLR 1.1 or 2.0 (path tells which); clr.dll = CLR 4. Logged once when seen.
    if (g_logging) {
        bool reported = false;
        for (int i = 0; i < 400 && !reported; i++) {
            const char* names[3] = { "mscorwks.dll", "clr.dll", "coreclr.dll" };
            for (int n = 0; n < 3; n++) {
                HMODULE clr = GetModuleHandleA(names[n]);
                if (clr) {
                    char p[MAX_PATH] = "?"; GetModuleFileNameA(clr, p, MAX_PATH);
                    logf("CLR loaded: %s  (%s)", names[n], p);
                    reported = true; break;
                }
            }
            if (!reported) Sleep(25);
        }
        if (!reported) logf("CLR: no managed runtime seen after 10s (native-only host?)");
    }

    int zeroMs = 0;
    unsigned supplied = 0;
    g_watchdogTid = GetCurrentThreadId();
    for (;;) {
        // At exit GvrIO_oem.dll is unloaded while this thread still runs: reading its heartbeat
        // then crashed the shell on exit (0xC0000005). The guard ends the thread instead. No
        // GetModuleHandle check here: it takes the loader lock, which the OEM teardown may hold
        // while it waits for this very heartbeat. Never stop earlier either: another cGvrIO may
        // be torn down later and needs it.
        __try {
            if (*flag == 0) {
                if (++zeroMs >= kZeroMsBeforeWeAct) {
                    *flag = 1;             // no worker is coming; unblock teardown
                    zeroMs = 0;
                    if (++supplied <= 8 || (supplied % 100) == 0)
                        logf("heartbeat supplied (#%u) - nothing acked within %d ms",
                             supplied, kZeroMsBeforeWeAct);
                }
            }
            else zeroMs = 0;               // a real worker is alive; stay out of it
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {   // GvrIO_oem.dll already unloaded
            logf("watchdog: GvrIO_oem.dll is gone - stopped");
            return 0;
        }
        Sleep(1);
    }
}

// ------------------------------------------------- shell selection (2.0.2 menus)
// 2.0.2's login / name-entry / cabinet-number screens read Gvr.SelectionX (msg 0x0F, device 2;
// SelectionY = 0x10) after setting Gvr.SelectionRangeX (msg 0x13): GvrIO turns the cabinet
// wheel into a slot 0..range-1 and raises "onSelectionChanged" through AMPlayer's event callback
// (event 0x0B, name at +0xC; 0x16 / 0x15 = "X / Y slot changed", pointer at +0xC). With no
// cabinet wheel its idle reading wanders, so the slot changed by itself about once a second and
// the screens scrolled with nobody touching them. When no real analog device answers, the shim
// owns the slot instead: GvrIO's own selection events are dropped, SelectionX/Y are answered
// from here, and keyboard Left/Right (D/G), the pad (stick, D-pad) or a PC wheel move it.
typedef void* (__cdecl *AmEventFn)(void*);
static AmEventFn     g_amCb = 0;               // AMPlayer's event callback (from Initialize)
static volatile LONG g_selOwn = -1;            // -1 not decided yet, 1 = the shim owns the slot
static volatile LONG g_selX = 0, g_selY = 0, g_rangeX = 1, g_rangeY = 1;

static void* __cdecl sel_filter_cb(void* m)
{
    if (g_selOwn == 1 && m) {
        int* f = (int*)m;
        if (f[1] == 0x15 || f[1] == 0x16) return 0;
        if (f[1] == 0x0B) {
            __try {
                const char* n = (const char*)(INT_PTR)f[3];
                if (n && strcmp(n, "onSelectionChanged") == 0) return 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    return g_amCb ? g_amCb(m) : 0;
}

static void sel_fire(void)
{
    int e[8] = { 0 };                          // the OEM sends these two, in this order
    e[1] = 0x16; e[3] = (int)(INT_PTR)&g_selX; g_amCb(e);
    e[1] = 0x0B; e[3] = (int)(INT_PTR)"onSelectionChanged"; g_amCb(e);
}

static DWORD WINAPI sel_thread(LPVOID)
{
    int lastDir = 0;
    DWORD nextStep = 0;
    for (;;) {
        Sleep(30);
        if (g_selOwn != 1 || !g_amCb) continue;
        int range = g_rangeX < 1 ? 1 : (int)g_rangeX;
        int v = 0, x = (int)g_selX;
        if (!pad_axis(0, &v) && pad_process_has_focus()) {
            if ((GetAsyncKeyState(VK_LEFT) | GetAsyncKeyState('D')) & 0x8000)  v = -60;
            if ((GetAsyncKeyState(VK_RIGHT) | GetAsyncKeyState('G')) & 0x8000) v = 60;
        }
        if (wheel_steers()) {
            x = (v + 127) * range / 255;       // a wheel picks by position, like the cabinet's
        } else {
            int dir = v < -30 ? -1 : v > 30 ? 1 : 0;
            DWORD now = GetTickCount();
            if (dir && (dir != lastDir || (int)(now - nextStep) >= 0)) {
                x += dir;
                nextStep = now + (dir != lastDir ? 400 : 150);   // first step, then auto-repeat
            }
            lastDir = dir;
        }
        if (x < 0) x = 0;
        if (x > range - 1) x = range - 1;
        if (x != g_selX) { InterlockedExchange(&g_selX, x); sel_fire(); }
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
        if (g_isShell && cb && !g_amCb) {      // shell selection: see sel_filter_cb
            g_amCb = (AmEventFn)cb;
            cb = (MessageSendFn)sel_filter_cb;
            CloseHandle(CreateThread(NULL, 0, sel_thread, NULL, 0, NULL));
        }
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

// Messages 0x47 / 0x5D / 0x5F use the OEM's IO-board object (global at RVA 0xE210 in the
// shipped GvrIO build, link stamp 0x480FA1CF), which only a real board creates (msgs 9 / 0x0A).
// Without one the OEM dereferences NULL - the 2.0.2 attract's msg 0x5F faulted at
// GvrIO_oem+0x2249 with the OEM's message lock still held, and the shell went down with it.
// Answer "no data" (NULL, as the OEM does before it is initialised) instead of forwarding.
static bool oem_board_missing(int id)
{
    if (id != 0x47 && id != 0x5D && id != 0x5F) return false;
    HMODULE oem = oem_module();
    if (!oem) return false;
    const BYTE* base = (const BYTE*)oem;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != 0x480FA1CF) return false;   // other build: forward as before
    return *(void* const*)(base + 0xE210) == NULL;
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
    int id = msg ? ((int*)msg)[1] : -1;
    if (oem_board_missing(id)) {
        static LONG warned[3];
        LONG* w = &warned[id == 0x47 ? 0 : id == 0x5D ? 1 : 2];
        if (InterlockedExchange(w, 1) == 0) logf("msg 0x%X needs the IO board object (none) - answering NULL", id);
        return 0;
    }
    if (g_isShell && g_amCb && id >= 0x0F && id <= 0x14 && oemFn) {
        // Decided at the first selection message; while the OEM owns it, a PC wheel or pedal box
        // found later (the controller scan runs in the background) still takes over - OEM
        // Steering.dll also reports a calibrated range for a PC wheel, so "OEM" alone was latched
        // when the first message beat the scan.
        if (g_selOwn < 0 || (g_selOwn == 0 && wheel_present())) {
            int t[8] = { 1, 5, 0 };
            LONG own = (wheel_present() || !real_pedals(oemFn, (sMessage*)t)) ? 1 : 0;
            if (own != g_selOwn) {
                InterlockedExchange(&g_selOwn, own);
                logf("menu selection: %s", own ? "keyboard / pad / PC wheel (no cabinet wheel)" : "cabinet wheel (OEM)");
            }
        }
        if (g_selOwn == 1) {
            int* f = (int*)msg;
            if (id == 0x0F || id == 0x10) {    // SelectionX / SelectionY: the value via +0x10
                volatile LONG* p = id == 0x0F ? &g_selX : &g_selY;
                f[4] = (int)(INT_PTR)p;
                return (void*)p;
            }
            void* rs = oemFn(msg);             // the setters still reach the OEM
            LONG v = f[2];
            if (id == 0x11) InterlockedExchange(&g_selX, v < 0 ? 0 : v);
            if (id == 0x12) InterlockedExchange(&g_selY, v < 0 ? 0 : v);
            if (id == 0x13) { InterlockedExchange(&g_rangeX, v < 1 ? 1 : v); if (g_selX >= g_rangeX) InterlockedExchange(&g_selX, g_rangeX - 1); }
            if (id == 0x14) InterlockedExchange(&g_rangeY, v < 1 ? 1 : v);
            return rs;
        }
    }
    void* r = oemFn ? oemFn(msg) : 0;
    if (!g_isShell) {
        // The race: every message passes through untouched, except that a controller
        // drives the live axes (msg 5, device 1) when no real wheel and pedals are fitted.
        // The race reads them every frame (FUN_0045c650) and uses any nonzero value in
        // place of its keyboard input - see GvrIOPad.cpp.
        if (id == 5 && ((int*)msg)[0] == 1 && oemFn) {
            static int s_raceAxis[3];
            int axis = ((int*)msg)[2], v = 0;
            // With no real wheel and pedals the answer is the controller, or 0 (= the race's
            // own keyboard input). 1.1's OEM answered 0 there anyway; 2.0.2 also loads the
            // force-feedback Steering.dll, whose idle value steered the car hard left.
            if (axis >= 0 && axis < 3 && (!r || wheel_present() || !real_pedals(oemFn, msg))) {
                bool pad = pad_axis(axis, &v);
                static int s_traced[3];
                if (s_traced[axis]++ < 3)
                    logf("race axis %d: OEM %d -> %s %d", axis, r ? *(int*)r : -1, pad ? "controller" : "keyboard", v);
                s_raceAxis[axis] = v;
                r = &s_raceAxis[axis];
            }
        }
        return r;
    }
    // PC pedals are calibrated like the cabinet's I/O board (GvrIOWheel.cpp): raw position
    // (msg 6 = Gvr.AxisRawY/Z), min / max (0x39 / 0x3A) and reset (0x53 = Gvr.ResetCalibration)
    // come from the shim, so the shell's own "Calibrate the accelerator and brake" screen (shown
    // while max - min < 40) and the operator Control Test (Start + Music) calibrate them.
    if (msg && ((int*)msg)[0] == 1 && (id == 6 || id == 0x39 || id == 0x3A || id == 0x53) &&
        GetEnvironmentVariableA("GVRIOSHIM_NO_IOBOARD", NULL, 0) == 0) {
        { int v; pad_axis(1, &v); }            // starts the pad thread, which scans for controllers
        wheel_discover_first();                // waits for that scan at most once, 3 s
        if (id == 0x53) {
            if (wheel_cal_reset()) logf("pedal calibration reset by the shell (msg 0x53)");
            return r;
        }
        // The calibration screen reads only these, so read the controller here (at most every
        // 10 ms) - nothing else polls it in the shell at that point and the raw value stood still.
        static DWORD s_polled = 0;
        DWORD now = GetTickCount();
        if (now - s_polled >= 10) { s_polled = now; int v; pad_axis(1, &v); }
        static int s_cal[3][3];                // [raw / min / max][axis]
        int axis = ((int*)msg)[2], raw, mn, mx;
        // Steering raw (Gvr.AxisRawX): the PC wheel's steering, -127..127 like the race's - the
        // operator Control Test draws its wheel from it (rotation = raw x -1.1 degrees) and ticks
        // left / right past +/-30. The cabinet's power-on motor self-test is not involved.
        if (id == 6 && axis == 0 && wheel_steers()) {
            int v = 0;
            pad_axis(0, &v);
            s_cal[0][0] = v;
            return &s_cal[0][0];
        }
        if (axis >= 1 && axis <= 2 && wheel_cal_axis(axis, &raw, &mn, &mx)) {
            int k = id == 6 ? 0 : id == 0x39 ? 1 : 2;
            s_cal[k][axis] = k == 0 ? raw : k == 1 ? mn : mx;
            return &s_cal[k][axis];
        }
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
            static int s_traced[3];
            if (s_traced[axis]++ < 3)
                logf("shell axis %d: OEM %d, wheel=%d, real pedals=%d", axis, r ? *(int*)r : -1,
                     (int)wheel_present(), (int)real_pedals(oemFn, msg));
            // A PC wheel (GvrIOWheel.cpp) always wins: with one attached, OEM Steering.dll reports a
            // calibrated range but only reads the wheel's X axis - its gas / brake would stay 0.
            if (!r || wheel_present() || !real_pedals(oemFn, msg)) {
                int v = 0;
                bool pad = pad_axis(axis, &v);
                if (axis == 0 && v == 0 && pad_process_has_focus()) {
                    if ((GetAsyncKeyState(VK_LEFT) | GetAsyncKeyState('D')) & 0x8000)  v = -60;
                    if ((GetAsyncKeyState(VK_RIGHT) | GetAsyncKeyState('G')) & 0x8000) v = 60;
                }
                // Keys and D-pad give a fixed small turn (60), which car / track select steps
                // through every 0.8 s; past 100 it steps every 0.1 s. Held for 0.7 s, report a
                // full turn - a tap still moves one step, a hold scrolls like a wheel turned far.
                if (axis == 0) {
                    static DWORD s_holdSince = 0;
                    if (v == 60 || v == -60) {
                        DWORD now = GetTickCount();
                        if (!s_holdSince) s_holdSince = now;
                        else if (now - s_holdSince > 700) v = v > 0 ? 110 : -110;
                    } else s_holdSince = 0;
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

// First-chance Vectored Exception Handler. The managed shell hosts the CLR, which installs its
// own top-level filter and routes fatal faults straight to WER -- so crash_filter (a UEF) never
// sees them and the log shows only "exited with code 0xC0000005" from the launcher. A VEH runs
// FIRST, before any frame-based or top-level handler, so it captures the faulting module+offset
// even when the CLR/WER would otherwise swallow the UEF. We only log and always CONTINUE_SEARCH,
// so real handling (including the game's own) is unchanged. Gated to a handful of fatal codes so
// the CLR's and C++ runtime's ordinary first-chance exceptions don't flood the trace; capped so a
// tight fault loop can't fill the disk. The terminating fault is the LAST line written.
static volatile LONG g_inVeh = 0;
static volatile LONG g_vehCount = 0;
static LONG WINAPI veh_handler(EXCEPTION_POINTERS* ep)
{
    EXCEPTION_RECORD* er = ep ? ep->ExceptionRecord : NULL;
    unsigned long code = er ? er->ExceptionCode : 0;
    // Only the codes that mean "something is already broken". Skip managed (0xE0434352) and
    // C++ (0xE06D7363) throws and everything else -- those are normal control flow here.
    if (code != EXCEPTION_ACCESS_VIOLATION &&      // 0xC0000005
        code != 0xC0000374UL &&                    // STATUS_HEAP_CORRUPTION
        code != 0xC00000FDUL &&                    // STATUS_STACK_OVERFLOW
        code != 0xC0000409UL &&                    // STATUS_STACK_BUFFER_OVERRUN / fail-fast
        code != EXCEPTION_ILLEGAL_INSTRUCTION &&   // 0xC000001D
        code != EXCEPTION_PRIV_INSTRUCTION)        // 0xC0000096
        return EXCEPTION_CONTINUE_SEARCH;
    if (code == EXCEPTION_ACCESS_VIOLATION && GetCurrentThreadId() == g_watchdogTid)
        return EXCEPTION_CONTINUE_SEARCH;          // the watchdog's guarded read at exit (WatchdogThread)
    if (InterlockedExchange(&g_inVeh, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;  // reentrancy
    if (InterlockedIncrement(&g_vehCount) <= 60) {
        __try {
            void* addr = er ? er->ExceptionAddress : NULL;
            char mod[MAX_PATH] = "?"; unsigned long off = 0; HMODULE hm = NULL;
            if (addr && GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    (LPCSTR)addr, &hm) && hm) {
                GetModuleFileNameA(hm, mod, MAX_PATH);
                off = (unsigned long)((DWORD_PTR)addr - (DWORD_PTR)hm);
            }
            const char* b = strrchr(mod, '\\'); b = b ? b + 1 : mod;
            logf("[VEH first-chance] code=0x%08lX addr=%p  %s+0x%lX", code, addr, b, off);
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
    }
    InterlockedExchange(&g_inVeh, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

// [Debug] Log: every DLL loaded from outside Windows and the install folder - overlays and hook
// DLLs (Discord, RivaTuner, recording / FPS tools, ReShade, Steam) are a classic cause of crashes in
// old OpenGL / DirectDraw programs. Listed 15 s and 60 s after start (some inject only when the
// window or the renderer appears); the second pass lists only newcomers.
static DWORD WINAPI ModuleScanThread(LPVOID)
{
    char win[MAX_PATH] = "", root[MAX_PATH] = "";
    GetWindowsDirectoryA(win, MAX_PATH);
    if (shim_settings_path(root, sizeof root)) { char* s = strrchr(root, '\\'); if (s) *s = 0; }
    static char seen[64][MAX_PATH]; int nSeen = 0;
    static const DWORD waits[2] = { 15000, 45000 };
    typedef BOOL (WINAPI *PFN_Enum)(HANDLE, HMODULE*, DWORD, LPDWORD);   // kernel32 (Win7+), else psapi (XP)
    PFN_Enum enumMods = (PFN_Enum)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32EnumProcessModules");
    if (!enumMods) { HMODULE ps = LoadLibraryA("psapi.dll"); if (ps) enumMods = (PFN_Enum)GetProcAddress(ps, "EnumProcessModules"); }
    if (!enumMods) return 0;
    for (int pass = 0; pass < 2; ++pass) {
        Sleep(waits[pass]);
        HMODULE mods[512]; DWORD need = 0;
        if (!enumMods(GetCurrentProcess(), mods, sizeof mods, &need)) continue;
        int n = (int)(need / sizeof(HMODULE)), found = 0;
        if (n > 512) n = 512;
        for (int i = 0; i < n; ++i) {
            char p[MAX_PATH];
            if (!GetModuleFileNameA(mods[i], p, MAX_PATH)) continue;
            if ((win[0] && _strnicmp(p, win, strlen(win)) == 0) || (root[0] && _strnicmp(p, root, strlen(root)) == 0)) continue;
            bool old = false;
            for (int k = 0; k < nSeen; ++k) if (_stricmp(seen[k], p) == 0) old = true;
            if (old) continue;
            if (nSeen < 64) lstrcpynA(seen[nSeen++], p, MAX_PATH);
            logf("modules: loaded from outside Windows and the game: %s", p);
            ++found;
        }
        if (pass == 0 && !found) logf("modules: nothing loaded from outside Windows and the game (no overlay / hook DLLs)");
    }
    return 0;
}

// [Debug] Log, front end: the Anark player's own renderer verdict, read from its globals once its
// renderer test has run (init flag set) - Direct3D 7 / OpenGL / software usable and whether its
// built-in hardware list matched - next to the saved preferences it starts from. With no usable
// Direct3D 7 device the 1.1 player greys out Direct3D AND OpenGL and draws in software.
//   1.1  (stamp 0x46AFA277, sub_63B408DE): init 0x36E580, D3D7 0x36E57F, GL 0x36E57E, SW 0x36E57D, list 0x36E57C
//   2.0.2 (stamp 0x47C34847, sub_63B44D50): init 0x415830, D3D7 0x41582F, GL 0x41582E, SW 0x41582D, list 0x41582C
static DWORD WINAPI RendererVerdictThread(LPVOID)
{
    const BYTE* host = (const BYTE*)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(host + ((const IMAGE_DOS_HEADER*)host)->e_lfanew);
    DWORD stamp = nt->FileHeader.TimeDateStamp, init, flags;   // flags = RVA of the list byte; +1 SW, +2 GL, +3 D3D7
    const char* prefs;
    if (stamp == 0x46AFA277) { init = 0x36E580; flags = 0x36E57C; prefs = "Software\\Anark\\Client\\3.0\\Preferences"; }
    else if (stamp == 0x47C34847) { init = 0x415830; flags = 0x41582C; prefs = "Software\\Anark\\Client\\4.0\\Preferences"; }
    else return 0;
    if (init >= nt->OptionalHeader.SizeOfImage) return 0;
    BYTE f[4] = { 0 }; bool done = false;
    for (int i = 0; i < 300 && !done; ++i) {       // up to 60 s
        Sleep(200);
        __try {
            done = host[init] != 0;
            if (done) memcpy(f, host + flags, 4);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    char cr[32] = "not set", gl[16] = "not set";
    HKEY k;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, prefs, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        DWORD v = 0, n = sizeof v, t = 0;
        if (RegQueryValueExA(k, "ClientRenderer", NULL, &t, (BYTE*)&v, &n) == ERROR_SUCCESS)
            _snprintf(cr, sizeof cr, "%lu%s", v, v == 2 ? " (OpenGL)" : v == 3 ? " (Direct3D)" : v == 4 ? " (software)" : "");
        char s[8] = ""; n = sizeof s;
        if (RegQueryValueExA(k, "DisableGL", NULL, &t, (BYTE*)s, &n) == ERROR_SUCCESS) lstrcpynA(gl, s, sizeof gl);
        RegCloseKey(k);
    }
    if (!done) { logf("renderer (game's own test): not run within 60 s"); return 0; }
    logf("renderer (game's own test): Direct3D 7 usable=%s, OpenGL usable=%s, software usable=%s, known-hardware list "
         "matched=%s; saved choice ClientRenderer=%s, DisableGL=%s%s", f[3] ? "yes" : "NO", f[2] ? "yes" : "NO",
         f[1] ? "yes" : "NO", f[0] ? "YES" : "no", cr, gl,
         (!f[3] && !f[2] && strcmp(cr, "not set") == 0) ? " -> the game draws in SOFTWARE (very slow, looks frozen)" : "");
    return 0;
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
            if (g_isShell) pad_log_early();    // the front-end hooks set before the log was open
            g_prevFilter = SetUnhandledExceptionFilter(crash_filter);
            // A VEH too: the managed shell's CLR shadows the UEF and sends fatal faults to WER,
            // so only a first-chance vectored handler reliably captures the module+offset there.
            AddVectoredExceptionHandler(1, veh_handler);
        }
        // Before anything in this process reads the registry: the race imports this DLL,
        // and NascarLaunch loads it into the front end before AMPlayer's first instruction.
        reg_attach();
        // Before the game's main() runs, so the gate sees a satisfied record and the
        // video manager is constructed with the raised mode limit.
        patch_dongle_gate();
        patch_mode_limit();
        if (g_isShell) patch_us_locale();      // before the CLR starts and reads the locale
        block_pc_restart();                    // both processes: the game never restarts the PC
        if (!g_isShell) patch_horn_stop();
        if (!g_isShell) patch_music_button();
        if (!g_isShell) patch_music_playlist();
        HANDLE h = CreateThread(NULL, 0, WatchdogThread, NULL, 0, NULL);
        if (h) CloseHandle(h);
        if (g_logging) { h = CreateThread(NULL, 0, ModuleScanThread, NULL, 0, NULL); if (h) CloseHandle(h); }
        if (g_logging && g_isShell) { h = CreateThread(NULL, 0, RendererVerdictThread, NULL, 0, NULL); if (h) CloseHandle(h); }
    }
    return TRUE;
}
