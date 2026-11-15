// ============================================================================
//  GvrGlu.cpp  -  glu32.dll for the menus (Shell\bin\glu32.dll)
//
//  Loads our GvrIO.dll into AMPlayer.exe without injection. AMPlayer imports
//  GLU32.dll, which Windows looks for next to the program first, so it gets
//  this file. Every glu function passes straight on to Windows' own glu32.dll.
//  glu32 was chosen because Windows' compatibility fixes never load it
//  themselves: with such a fix active (Windows adds one by itself, e.g. after a
//  crash), a version.dll next to the program is skipped for Windows' own.
//
//  When GvrIO.dll is loaded matters: before any of AMPlayer's own code (it
//  reads the private registry first), but after Windows has finished loading
//  AMPlayer's DLLs (GvrIO edits AMPlayer's import table). So this file only
//  hooks AMPlayer's first instruction; the hook loads GvrIO.dll, puts the
//  instruction back and continues. Nothing heavy runs inside DllMain.
//
//  GVRGLU_NO_PRELOAD=1 (developer override) turns the preload off.
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdio.h>

// marker the launcher looks for, to know the menus load GvrIO.dll by themselves
extern "C" const char g_marker[] = "GvrGlu: loads GvrIO.dll into the menus";

// ---------------------------------------------------------------- pass-through to Windows' glu32.dll
static const char* const kNames[] = {
    "gluBeginCurve",
    "gluBeginPolygon",
    "gluBeginSurface",
    "gluBeginTrim",
    "gluBuild1DMipmaps",
    "gluBuild2DMipmaps",
    "gluCylinder",
    "gluDeleteNurbsRenderer",
    "gluDeleteQuadric",
    "gluDeleteTess",
    "gluDisk",
    "gluEndCurve",
    "gluEndPolygon",
    "gluEndSurface",
    "gluEndTrim",
    "gluErrorString",
    "gluErrorUnicodeStringEXT",
    "gluGetNurbsProperty",
    "gluGetString",
    "gluGetTessProperty",
    "gluLoadSamplingMatrices",
    "gluLookAt",
    "gluNewNurbsRenderer",
    "gluNewQuadric",
    "gluNewTess",
    "gluNextContour",
    "gluNurbsCallback",
    "gluNurbsCurve",
    "gluNurbsProperty",
    "gluNurbsSurface",
    "gluOrtho2D",
    "gluPartialDisk",
    "gluPerspective",
    "gluPickMatrix",
    "gluProject",
    "gluPwlCurve",
    "gluQuadricCallback",
    "gluQuadricDrawStyle",
    "gluQuadricNormals",
    "gluQuadricOrientation",
    "gluQuadricTexture",
    "gluScaleImage",
    "gluSphere",
    "gluTessBeginContour",
    "gluTessBeginPolygon",
    "gluTessCallback",
    "gluTessEndContour",
    "gluTessEndPolygon",
    "gluTessNormal",
    "gluTessProperty",
    "gluTessVertex",
    "gluUnProject",
};

static FARPROC g_real[sizeof(kNames) / sizeof(kNames[0])];
static HMODULE g_sys;

static FARPROC WINAPI resolve(int i)
{
    if (!g_real[i]) {
        if (!g_sys) {
            char p[MAX_PATH];
            UINT n = GetSystemDirectoryA(p, MAX_PATH);          // SysWOW64 for this 32-bit DLL on 64-bit Windows
            if (n && n < MAX_PATH - 16) { strcat(p, "\\glu32.dll"); g_sys = LoadLibraryA(p); }
        }
        if (g_sys) g_real[i] = GetProcAddress(g_sys, kNames[i]);
        if (!g_real[i]) { SetLastError(ERROR_PROC_NOT_FOUND); return NULL; }
    }
    return g_real[i];
}

// each export: find the real function, then jump to it with the caller's stack untouched
#define STUB(i, name) \
    extern "C" __declspec(naked) void name##_stub(void) { __asm { push i } __asm { call resolve } \
        __asm { test eax, eax } __asm { jz fail } __asm { jmp eax } fail: __asm { xor eax, eax } __asm { ret } }
// (a failed lookup returns 0 = FALSE without popping arguments: only reached if Windows' glu32.dll is missing)
STUB(0, gluBeginCurve)
STUB(1, gluBeginPolygon)
STUB(2, gluBeginSurface)
STUB(3, gluBeginTrim)
STUB(4, gluBuild1DMipmaps)
STUB(5, gluBuild2DMipmaps)
STUB(6, gluCylinder)
STUB(7, gluDeleteNurbsRenderer)
STUB(8, gluDeleteQuadric)
STUB(9, gluDeleteTess)
STUB(10, gluDisk)
STUB(11, gluEndCurve)
STUB(12, gluEndPolygon)
STUB(13, gluEndSurface)
STUB(14, gluEndTrim)
STUB(15, gluErrorString)
STUB(16, gluErrorUnicodeStringEXT)
STUB(17, gluGetNurbsProperty)
STUB(18, gluGetString)
STUB(19, gluGetTessProperty)
STUB(20, gluLoadSamplingMatrices)
STUB(21, gluLookAt)
STUB(22, gluNewNurbsRenderer)
STUB(23, gluNewQuadric)
STUB(24, gluNewTess)
STUB(25, gluNextContour)
STUB(26, gluNurbsCallback)
STUB(27, gluNurbsCurve)
STUB(28, gluNurbsProperty)
STUB(29, gluNurbsSurface)
STUB(30, gluOrtho2D)
STUB(31, gluPartialDisk)
STUB(32, gluPerspective)
STUB(33, gluPickMatrix)
STUB(34, gluProject)
STUB(35, gluPwlCurve)
STUB(36, gluQuadricCallback)
STUB(37, gluQuadricDrawStyle)
STUB(38, gluQuadricNormals)
STUB(39, gluQuadricOrientation)
STUB(40, gluQuadricTexture)
STUB(41, gluScaleImage)
STUB(42, gluSphere)
STUB(43, gluTessBeginContour)
STUB(44, gluTessBeginPolygon)
STUB(45, gluTessCallback)
STUB(46, gluTessEndContour)
STUB(47, gluTessEndPolygon)
STUB(48, gluTessNormal)
STUB(49, gluTessProperty)
STUB(50, gluTessVertex)
STUB(51, gluUnProject)

// ---------------------------------------------------------------- GvrIO.dll at AMPlayer's first instruction
static BYTE  g_saved[5];
static BYTE* g_entry;
static char  g_gvrio[MAX_PATH];

static void __stdcall at_entry(void)
{
    DWORD old;
    if (VirtualProtect(g_entry, 5, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy(g_entry, g_saved, 5);
        VirtualProtect(g_entry, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_entry, 5);
    }
    if (!GetModuleHandleA("GvrIO.dll")) {
        SetEnvironmentVariableA("GVRGLU_PRELOADED", "1");     // set first, so the shim can see it while it loads
        LoadLibraryA(g_gvrio);
    }
    // tell NascarLaunch.exe it worked (it falls back to injection when this never appears); kept open on purpose
    if (GetModuleHandleA("GvrIO.dll")) {
        char name[64];
        _snprintf(name, sizeof(name) - 1, "Local\\GvrGlu_loaded_%lu", GetCurrentProcessId()); name[sizeof(name) - 1] = 0;
        CreateEventA(NULL, TRUE, TRUE, name);
    }
}

extern "C" __declspec(naked) void entry_hook(void)
{
    __asm {
        pushad
        pushfd
        call at_entry
        popfd
        popad
        jmp dword ptr [g_entry]
    }
}

static void hook_entry(HINSTANCE self)
{
    if (GetEnvironmentVariableA("GVRGLU_NO_PRELOAD", NULL, 0) > 0) return;
    char exe[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    const char* b = strrchr(exe, '\\');
    if (_stricmp(b ? b + 1 : exe, "AMPlayer.exe") != 0) return;          // only the menus
    GetModuleFileNameA(self, g_gvrio, MAX_PATH);
    char* s = strrchr(g_gvrio, '\\');
    if (!s || (size_t)(s - g_gvrio) + 12 >= MAX_PATH) return;
    strcpy(s + 1, "GvrIO.dll");
    if (GetFileAttributesA(g_gvrio) == INVALID_FILE_ATTRIBUTES) return;

    BYTE* base = (BYTE*)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE) return;
    g_entry = base + nt->OptionalHeader.AddressOfEntryPoint;
    DWORD old;
    if (!VirtualProtect(g_entry, 5, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(g_saved, g_entry, 5);
    g_entry[0] = 0xE9;
    *(LONG*)(g_entry + 1) = (LONG)((BYTE*)entry_hook - (g_entry + 5));
    VirtualProtect(g_entry, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_entry, 5);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        volatile char keep = g_marker[0]; (void)keep;    // keeps the marker in the file
        hook_entry(self);
    }
    return TRUE;
}
