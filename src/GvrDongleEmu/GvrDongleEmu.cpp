// ---------------------------------------------------------------------------
//  GvrDongleEmu  ->  GVRSCR28.dll   (NASCAR Team Racing, GlobalVR shell)
//
//  A software GVRStorageDevice back end that presents a permanently fitted
//  GVR DONGLE, so AMPlayer.exe (GvrShell) stops showing DongleError.am.
//
//  WHY THIS FILE EXISTS, AND WHY IT IS *NOT* GvrCardEmu
//  ----------------------------------------------------
//  There are two unrelated dongles in this game:
//
//    * the GAME (NASCAR_GVR.exe) checks an Aladdin HASP, statically linked, via
//      hasp(6,...). Already satisfied by the 39-byte stub in GvrIOShim.
//    * the SHELL checks a GVR SMARTCARD through GVRSCR28.dll -> PC/SC. That is
//      this file's job.
//
//  Measured, not assumed: breakpointing GVRStorageDevice::Initialize showed the
//  shell asking for "GVRSCR28.dll" ten times and never once for
//  DongleStorageDevice.dll. In this architecture "dongle" is not a separate DLL
//  - all four device kinds share one front end and one back end, and the only
//  difference is the byte GetType() returns:
//
//      GVRSDType:  0 = PLAYER CARD   1 = OPERATOR   2 = DONGLE   3 = HOURLY
//
//  NFSU's GIT\src\GvrCardEmu implements the same ABI, but for a *player card*:
//  it models insertion and ejection (tied to pressing START, an auto-eject
//  timer, a shared event, a companion PCSCSCR2.dll so CAREER is not greyed
//  out). A dongle is permanently fitted, so none of that belongs here and all
//  of it is deliberately absent. Shared ABI, separate implementation - the two
//  titles ship different 2008 builds and patched binaries never port between
//  them.
//
//  WHAT THE SHELL ACTUALLY REQUIRES
//  --------------------------------
//  PLUSDE's GvrSmartDevice.ValidateCard(id, type, bConnect) is, from its IL:
//
//      Connect() == 0  &&  (GetStatus(&s), s & 1)  &&  GetType() == type
//                      &&  GetId() == id
//
//  GlobalVR's own GVRSDEmulator.dll satisfies the first two and fails the last
//  two: GetType() returns 0 (PLAYER), and GetId() returns success WITHOUT EVER
//  WRITING its out-param, so the id comparison can never hold. That is why
//  swapping the stock emulator in, and why patching GetType to a constant 2,
//  both failed - neither can fix GetId. This implementation fixes both.
//
//  THE ABI (recovered in GVRSCR28_ABI_REPORT.md; identical layout to the stock
//  emulator, verified slot-by-slot)
//    void* CreateGVRStorageDeviceImp(void)    __cdecl, no args
//    void  ReleaseGVRStorageDeviceImp(void*)  __cdecl, one arg
//  The returned object is a plain vtable pointer; there is NO virtual
//  destructor, so slot 0 must be Initialize. All slots are __thiscall, which is
//  what the compiler emits for virtuals on x86 - hence a real C++ class here
//  rather than a hand-built table.
//
//  PERSISTENCE
//  The stock emulator mallocs and zeroes its image every process start, so the
//  card always reads back blank and nothing the shell writes survives. We back
//  the image with a file so registration and activation stick across restarts.
//
//  IT LOGS EVERYTHING. That is half the point: the shell gives no diagnostic of
//  its own, so the log is how we learn which calls it makes, in what order, and
//  where it gives up. Set GVRDONGLE_LOG=1.
//
//  Config (all optional):
//    GVRDONGLE_LOG=1        enable the trace
//    GVRDONGLE_TYPE=2       device type to report (default 2 = DONGLE)
//    GVRDONGLE_SERIAL=1     the id GetId() reports (default 1)
//    GVRDONGLE_IMAGE=<path> where the card image lives (default beside the host)
//
//  Deploy: keep the OEM as GVRSCR28_oem.dll, drop this in as GVRSCR28.dll.
//
//  This is local bookkeeping for an offline free-play cabinet. It implements a
//  documented storage interface and defeats no cryptography.
//  Build: build.cmd  (32-bit, static CRT - the host links MSVC 7.1)
// ---------------------------------------------------------------------------

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define CARD_SIZE      8192       // backing buffer (over-allocated; harmless)
#define DONGLE_SIZE    112        // GetSize(); the real DongleStorageDevice's HASP memory size
#define RC_OK          0
#define RC_ALREADYINIT 1003
#define RC_NOTINIT     1004
#define RC_RANGE       5000

#define SDTYPE_PLAYER   0
#define SDTYPE_OPERATOR 1
#define SDTYPE_DONGLE   2
#define SDTYPE_HOURLY   3

static CRITICAL_SECTION g_lock;
static BOOL  g_ready   = FALSE;
static char  g_logPath[MAX_PATH];
static char  g_imgPath[MAX_PATH];
static BOOL  g_logOn   = FALSE;
static int   g_type    = SDTYPE_DONGLE;
static __int64 g_serial = 1;

// ---- logging ---------------------------------------------------------------
// Plain Win32 file APIs: this DLL is loaded while the host is still starting up
// and we must not depend on the CRT's file handles being initialised yet.
static void Log(const char* fmt, ...)
{
    if (!g_logOn) return;
    char line[1024];
    SYSTEMTIME st; GetLocalTime(&st);
    int n = wsprintfA(line, "%02d:%02d:%02d.%03d  ",
                      st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    n += wvsprintfA(line + n, fmt, ap);
    va_end(ap);
    line[n++] = '\r'; line[n++] = '\n';

    HANDLE h = CreateFileA(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    DWORD written = 0;
    WriteFile(h, line, (DWORD)n, &written, NULL);
    CloseHandle(h);
}

// ---- the device ------------------------------------------------------------
// Slot order is fixed by the ABI and must not be reordered. Signatures match
// the GVRStorageDevice front end's exported members exactly.
class CGvrDongleEmu
{
public:
    CGvrDongleEmu();
    virtual int  Initialize(__int64 trustSig);          // 0
    virtual int  Shutdown();                            // 1
    virtual bool IsPresent();                           // 2
    virtual int  GetStatus(int& status);                // 3
    virtual int  Connect();                             // 4
    virtual int  Disconnect();                          // 5
    virtual int  GetId(__int64* id);                    // 6
    virtual unsigned char GetType();                    // 7
    virtual int  GetSize();                             // 8
    virtual int  GetManufacturerInfo(char* dst);        // 9
    virtual int  Format(char* label, int flags, __int64 sig);   // 10
    virtual bool SetAccessIndicator(bool on);           // 11
    virtual int  Read(int offset, int length, BYTE* dst);       // 12
    virtual int  Write(int offset, int length, const BYTE* src);// 13
    virtual int  ReadMagStripe(char* dst);              // 14

private:
    void LoadImage();
    void SaveImage();
    BYTE* m_img;
    BOOL  m_init;
};

CGvrDongleEmu::CGvrDongleEmu() : m_img(NULL), m_init(FALSE) {}

void CGvrDongleEmu::LoadImage()
{
    m_img = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, CARD_SIZE);
    if (!m_img) return;
    HANDLE h = CreateFileA(g_imgPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        Log("  no image at %s - starting from a blank one", g_imgPath);
        return;
    }
    DWORD got = 0;
    ReadFile(h, m_img, CARD_SIZE, &got, NULL);
    CloseHandle(h);
    Log("  loaded %lu byte(s) from %s", got, g_imgPath);
}

void CGvrDongleEmu::SaveImage()
{
    if (!m_img) return;
    HANDLE h = CreateFileA(g_imgPath, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { Log("  SaveImage failed (%lu)", GetLastError()); return; }
    DWORD wrote = 0;
    WriteFile(h, m_img, CARD_SIZE, &wrote, NULL);
    CloseHandle(h);
}

int CGvrDongleEmu::Initialize(__int64 trustSig)
{
    EnterCriticalSection(&g_lock);
    if (m_init) { LeaveCriticalSection(&g_lock); Log("Initialize -> already init (%d)", RC_ALREADYINIT); return RC_ALREADYINIT; }
    // The trust signature is ignored, exactly as the stock emulator ignores it.
    LoadImage();
    m_init = (m_img != NULL);
    LeaveCriticalSection(&g_lock);
    Log("Initialize(sig=0x%08lX%08lX) -> %d",
        (unsigned long)(trustSig >> 32), (unsigned long)trustSig, m_init ? RC_OK : RC_NOTINIT);
    return m_init ? RC_OK : RC_NOTINIT;
}

int CGvrDongleEmu::Shutdown()
{
    EnterCriticalSection(&g_lock);
    if (m_img) { SaveImage(); HeapFree(GetProcessHeap(), 0, m_img); m_img = NULL; }
    m_init = FALSE;
    LeaveCriticalSection(&g_lock);
    Log("Shutdown -> 0");
    return RC_OK;
}

// A dongle is bolted into the cabinet: it is always there.
bool CGvrDongleEmu::IsPresent() { Log("IsPresent -> true"); return true; }

int CGvrDongleEmu::GetStatus(int& status)
{
    // Bit 0 is the "present/ready" flag ValidateCard tests via (s & 1).
    status = m_init ? 1 : 8;
    Log("GetStatus -> status=%d rc=%d", status, m_init ? RC_OK : RC_NOTINIT);
    return m_init ? RC_OK : RC_NOTINIT;
}

int CGvrDongleEmu::Connect()    { Log("Connect -> %d", m_init ? RC_OK : RC_NOTINIT); return m_init ? RC_OK : RC_NOTINIT; }
int CGvrDongleEmu::Disconnect() { Log("Disconnect -> %d", m_init ? RC_OK : RC_NOTINIT); return m_init ? RC_OK : RC_NOTINIT; }

int CGvrDongleEmu::GetId(__int64* id)
{
    // THE whole point. The stock emulator returns success and never writes
    // here, so ValidateCard's "GetId() == id" can never hold.
    if (!id) { Log("GetId -> NULL out-param!"); return RC_NOTINIT; }
    *id = g_serial;
    Log("GetId -> %ld", (long)g_serial);
    return RC_OK;
}

unsigned char CGvrDongleEmu::GetType()
{
    // ValidateCard compares this against the type the CALLER asked for, so the
    // value decides which kind of device we appear to be. 2 = DONGLE is what
    // the shell's startup check wants; GVRDONGLE_TYPE overrides it so the same
    // binary can be used to probe the other paths.
    Log("GetType -> %d%s", g_type, g_type == SDTYPE_DONGLE ? " (DONGLE)" : "");
    return (unsigned char)g_type;
}

// The real DongleStorageDevice reports 112: the HASP memory is 112 bytes, read
// as 16-bit words. GvrSmartDeviceSchema parses Header.* within this window, so
// the reported size must match or ReadHeader bounds-checks will reject fields.
int CGvrDongleEmu::GetSize() { return DONGLE_SIZE; }

int CGvrDongleEmu::GetManufacturerInfo(char* dst)
{
    if (dst) lstrcpyA(dst, "GVR Dongle (software)");
    Log("GetManufacturerInfo");
    return RC_OK;
}

int CGvrDongleEmu::Format(char* label, int flags, __int64 sig)
{
    EnterCriticalSection(&g_lock);
    if (m_img) { FillMemory(m_img, CARD_SIZE, 0); SaveImage(); }
    LeaveCriticalSection(&g_lock);
    Log("Format(label=%s flags=%d sig=0x%08lX) -> 0",
        label ? label : "(null)", flags, (unsigned long)sig);
    if (label) lstrcpyA(label, "GVR Dongle");
    return RC_OK;
}

bool CGvrDongleEmu::SetAccessIndicator(bool on) { Log("SetAccessIndicator(%d)", on ? 1 : 0); return true; }

int CGvrDongleEmu::Read(int offset, int length, BYTE* dst)
{
    if (!m_init || !m_img) { Log("Read(%d,%d) -> not init", offset, length); return RC_NOTINIT; }
    if (offset < 0 || length <= 0 || offset >= CARD_SIZE || offset + length > CARD_SIZE) {
        Log("Read(%d,%d) -> RANGE", offset, length);
        return RC_RANGE;
    }
    EnterCriticalSection(&g_lock);
    CopyMemory(dst, m_img + offset, length);
    LeaveCriticalSection(&g_lock);
    // Log a little of the payload: this is how we see what the shell expects to
    // find in the header, which is schema-driven rather than fixed.
    Log("Read(%d,%d) -> ok  [%02X %02X %02X %02X %02X %02X %02X %02X]",
        offset, length,
        dst[0], length>1?dst[1]:0, length>2?dst[2]:0, length>3?dst[3]:0,
        length>4?dst[4]:0, length>5?dst[5]:0, length>6?dst[6]:0, length>7?dst[7]:0);
    return RC_OK;
}

int CGvrDongleEmu::Write(int offset, int length, const BYTE* src)
{
    if (!m_init || !m_img) { Log("Write(%d,%d) -> not init", offset, length); return RC_NOTINIT; }
    if (offset < 0 || length <= 0 || offset >= CARD_SIZE || offset + length > CARD_SIZE) {
        Log("Write(%d,%d) -> RANGE", offset, length);
        return RC_RANGE;
    }
    EnterCriticalSection(&g_lock);
    CopyMemory(m_img + offset, src, length);
    SaveImage();            // persist immediately: the host may never Shutdown cleanly
    LeaveCriticalSection(&g_lock);
    Log("Write(%d,%d) -> ok [%02X %02X %02X %02X]",
        offset, length, src[0], length>1?src[1]:0, length>2?src[2]:0, length>3?src[3]:0);
    return RC_OK;
}

int CGvrDongleEmu::ReadMagStripe(char* dst)
{
    if (dst) *dst = 0;
    Log("ReadMagStripe -> empty");
    return RC_OK;
}

// ---- config / paths --------------------------------------------------------
static void ResolveConfig()
{
    if (g_ready) return;
    g_ready = TRUE;

    char v[MAX_PATH];
    // Default ON. NascarLaunch.exe builds a custom child environment (it injects
    // GVRSQLITE_DB_NAS1) and does not forward GVRDONGLE_LOG, so keying the trace
    // off that env var left no log at all. For a diagnostic build we always log;
    // GVRDONGLE_LOG=0 can still turn it off explicitly.
    g_logOn = TRUE;
    if (GetEnvironmentVariableA("GVRDONGLE_LOG", v, sizeof(v)) > 0 && v[0] == '0')
        g_logOn = FALSE;

    // Default both files next to the host executable, which is inside the
    // portable install - so nothing leaks outside the install folder.
    char host[MAX_PATH]; host[0] = 0;
    GetModuleFileNameA(NULL, host, MAX_PATH);
    char* slash = host;
    for (char* p = host; *p; p++) if (*p == '\\' || *p == '/') slash = p;
    *slash = 0;

    wsprintfA(g_logPath, "%s\\gvrdongle.log", host);
    if (GetEnvironmentVariableA("GVRDONGLE_IMAGE", v, sizeof(v)) > 0) lstrcpynA(g_imgPath, v, MAX_PATH);
    else wsprintfA(g_imgPath, "%s\\gvrdongle.img", host);

    if (GetEnvironmentVariableA("GVRDONGLE_TYPE", v, sizeof(v)) > 0) {
        int t = 0; for (char* p = v; *p >= '0' && *p <= '9'; p++) t = t * 10 + (*p - '0');
        g_type = t;
    }
    if (GetEnvironmentVariableA("GVRDONGLE_SERIAL", v, sizeof(v)) > 0) {
        __int64 s = 0; for (char* p = v; *p >= '0' && *p <= '9'; p++) s = s * 10 + (*p - '0');
        if (s) g_serial = s;
    }
    Log("--- GvrDongleEmu loaded into %s ---", host);
    Log("  type=%d serial=%ld image=%s", g_type, (long)g_serial, g_imgPath);
}

// ---- exports ---------------------------------------------------------------
extern "C" void* __cdecl CreateGVRStorageDeviceImp(void)
{
    ResolveConfig();
    CGvrDongleEmu* p = new CGvrDongleEmu();
    Log("CreateGVRStorageDeviceImp -> %p", p);
    return p;
}

extern "C" void __cdecl ReleaseGVRStorageDeviceImp(void* p)
{
    Log("ReleaseGVRStorageDeviceImp(%p)", p);
    delete (CGvrDongleEmu*)p;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        InitializeCriticalSection(&g_lock);
    } else if (reason == DLL_PROCESS_DETACH) {
        DeleteCriticalSection(&g_lock);
    }
    return TRUE;
}
