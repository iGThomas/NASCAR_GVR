# Minimal DebugView: capture OutputDebugString from all processes for N seconds.
import ctypes, ctypes.wintypes as W, sys, time, struct
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.CreateEventW.restype=W.HANDLE; k.CreateFileMappingW.restype=W.HANDLE; k.MapViewOfFile.restype=ctypes.c_void_p
k.CreateFileMappingW.argtypes=[W.HANDLE,ctypes.c_void_p,W.DWORD,W.DWORD,W.DWORD,W.LPCWSTR]
k.MapViewOfFile.argtypes=[W.HANDLE,W.DWORD,W.DWORD,W.DWORD,ctypes.c_size_t]
ready=k.CreateEventW(None,False,False,"DBWIN_BUFFER_READY")
data=k.CreateEventW(None,False,False,"DBWIN_DATA_READY")
mp=k.CreateFileMappingW(W.HANDLE(-1),None,4,0,4096,"DBWIN_BUFFER")
view=k.MapViewOfFile(mp,4,0,0,4096)
if not (ready and data and mp and view): print("setup failed",ctypes.get_last_error()); sys.exit(1)
out=open(sys.argv[1],'w',encoding='utf-8'); end=time.time()+float(sys.argv[2])
print("listening",flush=True)
while time.time()<end:
    k.SetEvent(ready)
    if k.WaitForSingleObject(data,500)==0:
        pid=struct.unpack('<I',ctypes.string_at(view,4))[0]
        msg=ctypes.string_at(view+4).decode('latin1').rstrip()
        out.write(f"{time.strftime('%H:%M:%S')} [{pid}] {msg}\n"); out.flush()
