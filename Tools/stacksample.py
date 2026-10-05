# Sample the main thread of a WOW64 process: EIP + heuristic return addresses -> module+offset.
import ctypes, ctypes.wintypes as W, sys, struct
k=ctypes.WinDLL('kernel32',use_last_error=True); ps=ctypes.WinDLL('psapi')
pid=int(sys.argv[1]); samples=int(sys.argv[2]) if len(sys.argv)>2 else 3
PROCESS_ALL=0x1F0FFF; THREAD_ALL=0x1F03FF
k.OpenProcess.restype=W.HANDLE; k.OpenThread.restype=W.HANDLE
hp=k.OpenProcess(PROCESS_ALL,False,pid)
# modules (32-bit)
mods=(W.HMODULE*1024)(); need=W.DWORD()
ps.EnumProcessModulesEx(hp,mods,ctypes.sizeof(mods),ctypes.byref(need),1)
class MI(ctypes.Structure): _fields_=[('base',ctypes.c_void_p),('size',W.DWORD),('entry',ctypes.c_void_p)]
M=[]
for h in mods[:need.value//ctypes.sizeof(W.HMODULE)]:
    if not h: continue
    mi=MI(); ps.GetModuleInformation(hp,W.HMODULE(h),ctypes.byref(mi),ctypes.sizeof(mi))
    nm=ctypes.create_unicode_buffer(260); ps.GetModuleBaseNameW(hp,W.HMODULE(h),nm,260)
    M.append((mi.base or 0,mi.size,nm.value))
def where(a):
    for b,s,n in M:
        if b<=a<b+s: return f"{n}+{a-b:#x}"
    return None
# threads
class TE(ctypes.Structure): _fields_=[('sz',W.DWORD),('u',W.DWORD),('tid',W.DWORD),('owner',W.DWORD),('pri',W.LONG),('d',W.LONG),('f',W.DWORD)]
snap=k.CreateToolhelp32Snapshot(4,0); te=TE(); te.sz=ctypes.sizeof(te); tids=[]
k.CreateToolhelp32Snapshot.restype=W.HANDLE
ok=k.Thread32First(snap,ctypes.byref(te))
while ok:
    if te.owner==pid: tids.append(te.tid)
    ok=k.Thread32Next(snap,ctypes.byref(te))
# main thread = earliest creation time
def ctime(tid):
    h=k.OpenThread(THREAD_ALL,False,tid); a=(W.FILETIME*4)(); k.GetThreadTimes(h,*[ctypes.byref(a[i]) for i in range(4)]); k.CloseHandle(h)
    return (a[0].dwHighDateTime<<32)|a[0].dwLowDateTime
tid=int(sys.argv[3],0) if len(sys.argv)>3 else sorted(tids,key=ctime)[0]; print('threads',len(tids),'sampling tid',tid, 'present' if tid in tids else 'MISSING')
# WOW64_CONTEXT: ContextFlags at 0, ... Eip at 0xB8, Esp at 0xC4, Ebp at 0xB4
buf=(ctypes.c_ubyte*0x2cc)()
for n in range(samples):
    h=k.OpenThread(THREAD_ALL,False,tid); k.Wow64SuspendThread(h)
    struct.pack_into('<I',buf,0,0x10007)  # CONTEXT_i386 | CONTROL|INTEGER|SEGMENTS
    k.Wow64GetThreadContext(h,buf)
    eip,ebp,esp=struct.unpack_from('<I',buf,0xB8)[0],struct.unpack_from('<I',buf,0xB4)[0],struct.unpack_from('<I',buf,0xC4)[0]
    raw=(ctypes.c_ubyte*0x3000)(); rd=ctypes.c_size_t()
    k.ReadProcessMemory(hp,ctypes.c_void_p(esp),raw,0x3000,ctypes.byref(rd))
    k.ResumeThread(h); k.CloseHandle(h)
    print(f'--- sample {n}: EIP {where(eip) or hex(eip)}')
    seen=0
    for off in range(0,rd.value,4):
        v=struct.unpack_from('<I',raw,off)[0]; w=where(v)
        if w and not w.startswith(('KERNELBASE','ntdll')) or (w and seen<4 and w.startswith(('KERNELBASE','ntdll'))):
            print(f'   [esp+{off:#06x}] {w}'); seen+=1
            if seen>40: break
    import time; time.sleep(1)
