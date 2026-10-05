// AKLogShim - diagnostic stand-in for Anark's developer-only AKLog2 COM server.
// AMPlayer.exe CoCreateInstance({DE411D6F-2BDA-444C-AC87-9171BAFD9E99}, IID {18725949-89C4-460C-A092-75520BFFB542})
// and calls: +0x08 Release, +0x1C (a,b), +0x24 (), +0x28 (record*). Everything is forwarded to
// OutputDebugString as "AKLOG:" lines. Only AMPlayer gets an object; the AKPlu*.dll plug-ins (which
// may use other slots with unknown signatures) are refused.
#include <windows.h>
#include <stdio.h>

static const CLSID CLSID_AKLog2 = {0xDE411D6F,0x2BDA,0x444C,{0xAC,0x87,0x91,0x71,0xBA,0xFD,0x9E,0x99}};
static const IID   IID_AKLog2   = {0x18725949,0x89C4,0x460C,{0xA0,0x92,0x75,0x52,0x0B,0xFF,0xB5,0x42}};
static HMODULE g_self;

static void out(const char* s) { OutputDebugStringA(s); }
static void outf(const char* f, ...) { char b[1200]; va_list a; va_start(a,f); _vsnprintf(b,sizeof b-1,f,a); b[sizeof b-1]=0; va_end(a); out(b); }

static bool readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION m; if (!p || !VirtualQuery(p,&m,sizeof m)) return false;
    if (m.State!=MEM_COMMIT || (m.Protect & (PAGE_NOACCESS|PAGE_GUARD))) return false;
    return (const char*)p + n <= (const char*)m.BaseAddress + m.RegionSize;
}
// Print printable ASCII / UTF-16 runs found in a block.
static void strings_in(const char* tag, const unsigned char* p, size_t n) {
    char run[600]; size_t r=0;
    for (size_t i=0;i<=n;i++) {
        unsigned char c = i<n ? p[i] : 0;
        if (c>=0x20 && c<0x7f && r<sizeof run-1) run[r++]=c;
        else { if (r>=3) { run[r]=0; outf("AKLOG:%s +%#x \"%s\"\n",tag,(unsigned)(i-r),run);} r=0; }
    }
    for (size_t i=0;i+1<n;i+=2) { // UTF-16
        size_t j=i; r=0;
        while (j+1<n && p[j+1]==0 && p[j]>=0x20 && p[j]<0x7f && r<sizeof run-1) { run[r++]=p[j]; j+=2; }
        if (r>=4) { run[r]=0; outf("AKLOG:%s +%#x w\"%s\"\n",tag,(unsigned)i,run); i=j; }
    }
}
static void maybe_str(const char* tag, DWORD v) {
    if (readable((void*)v,4)) {
        const char* s=(const char*)v; size_t k=0; while (k<512 && readable(s+k,1) && s[k]>=0x20 && s[k]<0x7f) k++;
        if (k>=3) { outf("AKLOG:%s \"%.*s\"\n",tag,(int)k,s); return; }
        const wchar_t* w=(const wchar_t*)v; k=0; while (k<512 && readable(w+k,2) && w[k]>=0x20 && w[k]<0x7f) k++;
        if (k>=3) { char b[600]; WideCharToMultiByte(CP_ACP,0,w,(int)k,b,sizeof b,0,0); b[k<599?k:599]=0; outf("AKLOG:%s w\"%s\"\n",tag,b); return; }
    }
    outf("AKLOG:%s %#lx\n",tag,v);
}


// ---- first-chance exception logger (diagnostic) ----
static __declspec(thread) int t_inveh;
static void modoff(DWORD a, char* o, size_t n) {
    HMODULE m=0; char p[MAX_PATH]="?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)a,&m)) {
        GetModuleFileNameA(m,p,MAX_PATH); const char* b=strrchr(p,92); _snprintf(o,n,"%s+%#lx",b?b+1:p,a-(DWORD)m);
    } else _snprintf(o,n,"%#lx",a);
}
static LONG CALLBACK Veh(PEXCEPTION_POINTERS e) {
    DWORD code=e->ExceptionRecord->ExceptionCode;
    if (t_inveh || code==0x40010006 || code==0x4001000A || code==0x406D1388 || code==0x80000003) return EXCEPTION_CONTINUE_SEARCH;
    t_inveh=1;
    char where[300]; modoff((DWORD)e->ExceptionRecord->ExceptionAddress,where,sizeof where);
    char extra[900]=""; 
    if (code==0xE06D7363 && e->ExceptionRecord->NumberParameters>=3) {
        DWORD ti=(DWORD)e->ExceptionRecord->ExceptionInformation[2];
        __try { DWORD cta=*(DWORD*)(ti+0xC); DWORD ct=*(DWORD*)(cta+4); DWORD td=*(DWORD*)(ct+4); _snprintf(extra,sizeof extra," c++ type %s",(char*)(td+8)); } __except(1) {}
    }
    if (code==0xE0434F4D) { char pb[200]; int L=0; for (DWORD k=0;k<e->ExceptionRecord->NumberParameters && k<6;k++) L+=_snprintf(pb+L,sizeof pb-L," p%lu=%08lx",k,(DWORD)e->ExceptionRecord->ExceptionInformation[k]); strncat(extra,pb,sizeof extra-strlen(extra)-1); }
    if (code==0xE0434F4D) { DWORD* sp=(DWORD*)e->ContextRecord->Esp; char nm[400]=""; size_t L=0;
        for (int k=0;k<0x1000 && L<380;k++) { if (!readable(sp+k,4)) break; const char* q=(const char*)sp[k];
            if (readable(q,12) && q[0]>='A'&&q[0]<='Z'&&q[1]>='A'&&q[1]<='Z') { int m=0; while (m<40 && readable(q+m,1) && ((q[m]>='A'&&q[m]<='Z')||q[m]=='_'||(q[m]>='0'&&q[m]<='9'))) m++;
                if (m>=8 && q[m]==0 && strchr(q,'_') && strchr(q,'_')<q+m) L+=_snprintf(nm+L,sizeof nm-L," %.*s",m,q); } }
        strncat(extra," names:",sizeof extra-strlen(extra)-1); strncat(extra,nm,sizeof extra-strlen(extra)-1); }
    outf("AKLOG:EXC %08lx at %s%s\n",code,where,extra);
    void* fr[24]; USHORT n=CaptureStackBackTrace(1,24,fr,0); char line[1200]="AKLOG:EXC   stack:"; size_t L=strlen(line);
    for (USHORT i=0;i<n && L<1100;i++) { char w[120]; modoff((DWORD)fr[i],w,sizeof w); if (strncmp(w,"ntdll",5)&&strncmp(w,"KERNELBASE",10)&&strncmp(w,"AKLogShim",9)) L+=_snprintf(line+L,sizeof line-L," %s",w); }
    strcat(line,"\n"); out(line);
    t_inveh=0;
    return EXCEPTION_CONTINUE_SEARCH;
}

struct Log;
typedef HRESULT (__stdcall *Fn0)(Log*);
struct Log { void** vt; LONG ref; };
static HRESULT __stdcall QI(Log* t, REFIID r, void** o) {
    if (IsEqualIID(r,IID_IUnknown)||IsEqualIID(r,IID_AKLog2)) { *o=t; InterlockedIncrement(&t->ref); return S_OK; }
    *o=0; return E_NOINTERFACE;
}
static ULONG __stdcall AddRef(Log* t) { return InterlockedIncrement(&t->ref); }
static ULONG __stdcall Release(Log* t) { LONG r=InterlockedDecrement(&t->ref); if(!r) delete t; return r; }
static HRESULT __stdcall Slot(Log*) { return S_OK; }                              // unused slots, 0 args
static HRESULT __stdcall Slot1C(Log*, DWORD a, DWORD b) { maybe_str("1C.a",a); maybe_str("1C.b",b); return S_OK; }
static HRESULT __stdcall Slot24(Log*) { out("AKLOG:24 shutdown\n"); return S_OK; }
static HRESULT __stdcall Slot28(Log*, DWORD rec) {
    if (!readable((void*)rec,0x42c)) { maybe_str("28.rec",rec); return S_OK; }
    const DWORD* d=(const DWORD*)rec;
    outf("AKLOG:28 ---- %08lx %08lx %08lx %08lx\n",d[0],d[1],d[2],d[3]);
    // "Script Error: %s(%ld)" and similar: the arguments are not in the record as text, so
    // treat every dword as a possible va_list and print (string, number) pairs it points at.
    {
        const wchar_t* fmt = (const wchar_t*)(rec + 8);
        if (readable(fmt, 32) && fmt[0] == L'S' && fmt[1] == L'c' && fmt[6] == L' ') {
            for (int i = 0; i < 0x42c/4; i++) {
                const DWORD* va = (const DWORD*)d[i];
                if (d[i] < 0x10000 || !readable(va, 8)) continue;
                DWORD s0 = va[0], n1 = va[1];
                if (s0 < 0x10000 || !readable((void*)s0, 8) || n1 > 1000000) continue;
                const wchar_t* w = (const wchar_t*)s0; const char* a = (const char*)s0;
                char b[700]; b[0] = 0;
                if (w[0] >= 0x20 && w[0] < 0x7f && w[1] >= 0x20 && w[1] < 0x7f && w[2] >= 0x20 && w[2] < 0x7f && ((const char*)w)[1] == 0) {
                    int k = 0; while (k < 600 && readable(w + k, 2) && w[k] >= 0x20 && w[k] < 0x7f) k++;
                    if (k >= 4) { WideCharToMultiByte(CP_ACP, 0, w, k, b, sizeof b, 0, 0); b[k] = 0; }
                } else if (a[0] >= 0x20 && a[0] < 0x7f && a[1] >= 0x20 && a[1] < 0x7f) {
                    int k = 0; while (k < 600 && readable(a + k, 1) && a[k] >= 0x20 && a[k] < 0x7f) k++;
                    if (k >= 4) { memcpy(b, a, k); b[k] = 0; }
                }
                if (b[0]) outf("AKLOG:SCRIPTERR [rec+%#x] \"%s\" (%lu)\n", i * 4, b, n1);
            }
        }
    }
    strings_in("28",(const unsigned char*)rec,0x42c);
    for (int i=0;i<0x42c/4;i++) if (d[i]>0x10000 && readable((void*)d[i],8) && ((d[i]<rec)||(d[i]>=rec+0x42c))) {
        char tag[24]; sprintf(tag,"28.p%03x",i*4); const char* s=(const char*)d[i];
        if ((s[0]>=0x20&&s[0]<0x7f&&s[1]>=0x20&&s[1]<0x7f)||(s[1]==0&&s[0]>=0x20&&s[0]<0x7f&&s[3]==0)) maybe_str(tag,d[i]);
    }
    return S_OK;
}
static void* g_vt[16] = { (void*)QI,(void*)AddRef,(void*)Release,(void*)Slot,(void*)Slot,(void*)Slot,(void*)Slot,
                          (void*)Slot1C,(void*)Slot,(void*)Slot24,(void*)Slot28,(void*)Slot,(void*)Slot,(void*)Slot,(void*)Slot,(void*)Slot };

// Refuse anyone but AMPlayer.exe: walk the stack for an AKPlu*.dll caller.
static bool caller_is_plugin() {
    void* fr[48]; USHORT n=CaptureStackBackTrace(0,48,fr,0);
    for (USHORT i=0;i<n;i++) {
        HMODULE m=0; if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)fr[i],&m)) continue;
        char p[MAX_PATH]; GetModuleFileNameA(m,p,MAX_PATH); const char* b=strrchr(p,'\\'); b=b?b+1:p;
        if (!_strnicmp(b,"AKPlu",5)) return true;
    }
    return false;
}
struct CF { void** vt; };
static HRESULT __stdcall CF_QI(CF* t, REFIID r, void** o) { if (IsEqualIID(r,IID_IUnknown)||IsEqualIID(r,IID_IClassFactory)) { *o=t; return S_OK; } *o=0; return E_NOINTERFACE; }
static ULONG __stdcall CF_AddRef(CF*) { return 2; }
static ULONG __stdcall CF_Release(CF*) { return 1; }
static HRESULT __stdcall CF_Create(CF*, IUnknown* outer, REFIID r, void** o) {
    *o=0; if (outer) return CLASS_E_NOAGGREGATION;
    if (caller_is_plugin()) { out("AKLOG: refused instance for an AKPlu plug-in\n"); return E_NOINTERFACE; }
    Log* l=new Log; l->vt=g_vt; l->ref=0; HRESULT h=QI(l,r,o); if (FAILED(h)) delete l;
    else out("AKLOG: === AKLog2 stand-in created for AMPlayer ===\n");
    return h;
}
static HRESULT __stdcall CF_Lock(CF*, BOOL) { return S_OK; }
static void* g_cfvt[5] = { (void*)CF_QI,(void*)CF_AddRef,(void*)CF_Release,(void*)CF_Create,(void*)CF_Lock };
static CF g_cf = { g_cfvt };

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID c, REFIID r, void** o) {
    if (!IsEqualCLSID(c,CLSID_AKLog2)) return CLASS_E_CLASSNOTAVAILABLE;
    return CF_QI(&g_cf,r,o);
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return S_FALSE; }
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) { if (r==DLL_PROCESS_ATTACH) { g_self=h; DisableThreadLibraryCalls(h); AddVectoredExceptionHandler(1,Veh); out("AKLOG: exception logger armed\n"); } return TRUE; }
