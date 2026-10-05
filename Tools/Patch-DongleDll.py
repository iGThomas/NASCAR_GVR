r"""Patch-DongleDll.py - satisfy the SHELL's HASP dongle read in Shell\bin\Dongle.dll.

The shell's startup self-test reads the key through the GvrIO plug-in Dongle.dll, which carries
the SAME statically linked HASP record library as NASCAR_GVR.exe (NASCAR_FINDINGS.md 8.3):
    0x10004660(idx)  zero the 27-byte record at 0x10006068 + idx*0x1b, call the filler,
                     on success record[0] = 1                 (= game's FUN_00672460)
    0x10004410(rec)  filler: hasp(1), hasp(5), hasp(0x32) reads the 112-byte memory M, then
                     rec[1..6]=M[0..5] game, rec[8..A]=M[6..8] version, rec[C..D]=M[13..14]
                     region, rec[10..13]=M[9..12], rec[15..17]=M[16..18], rec[19]=M[15]
Replaces ONLY the filler with a 28-byte PIC stub copying a record precomputed from a memory
image (default "NASCAR1.5CTRYXX_CAB0"); relocations inside the stub are neutralised.
Idempotent: always patches from Dongle_oem.dll; refuses if the original bytes differ.
Usage: python Patch-DongleDll.py <dir> [--image ...] | <dir> --restore
"""
import argparse, os, shutil, struct, sys
FILLER_RVA = 0x4410
FILLER_SIG = bytes.fromhex("81ec84000000a12460001053558bac24")

def build_record(image):
    m = image.ljust(112, b"\0"); rec = bytearray(27)
    rec[0x01:0x07] = m[0:6];  rec[0x08:0x0B] = m[6:9];  rec[0x0C:0x0E] = m[13:15]
    rec[0x10:0x14] = m[9:13]; rec[0x15:0x18] = m[16:19]; rec[0x19] = m[15]
    return bytes(rec)

def build_stub(record):
    data = record[1:]
    code = bytes.fromhex("56" "57" "8b7c240c" "47" "e800000000" "5e" "83c610"
                         "b9" + struct.pack("<I", len(data)).hex() + "f3a4" "5f" "5e" "b001" "c3")
    assert len(code) == 28
    return code + data

def neutralise_relocs(buf, pe, lo, hi):
    n = 0
    for blk in pe.DIRECTORY_ENTRY_BASERELOC:
        for e in blk.entries:
            if lo <= e.rva < hi and e.type != 0:
                off = e.struct.get_file_offset()
                w = struct.unpack_from("<H", buf, off)[0]
                struct.pack_into("<H", buf, off, w & 0x0FFF); n += 1
    return n

def main():
    import pefile
    ap = argparse.ArgumentParser(); ap.add_argument("dir")
    ap.add_argument("--image", default="NASCAR1.5CTRYXX_CAB0"); ap.add_argument("--restore", action="store_true")
    a = ap.parse_args()
    dll = os.path.join(a.dir, "Dongle.dll"); oem = os.path.join(a.dir, "Dongle_oem.dll")
    if a.restore:
        if os.path.exists(oem): shutil.copy2(oem, dll); os.remove(oem); print("restored", dll)
        else: print("nothing to restore")
        return
    if not os.path.exists(oem): shutil.copy2(dll, oem); print("kept original as", oem)
    buf = bytearray(open(oem, "rb").read()); pe = pefile.PE(data=bytes(buf))
    off = pe.get_offset_from_rva(FILLER_RVA)
    if bytes(buf[off:off+16]) != FILLER_SIG: sys.exit("refusing: filler bytes do not match the known build")
    rec = build_record(a.image.encode("ascii")); stub = build_stub(rec)
    buf[off:off+len(stub)] = stub
    n = neutralise_relocs(buf, pe, FILLER_RVA, FILLER_RVA + len(stub))
    open(dll, "wb").write(buf)
    s = lambda b: b.split(b"\0")[0].decode("ascii", "replace")
    print(f"patched {dll}: {len(stub)}-byte stub at RVA 0x{FILLER_RVA:X}, {n} reloc(s) neutralised")
    print(f"  game={s(rec[1:8])} version={s(rec[8:12])} region={s(rec[12:15])} f10={s(rec[16:21])} f15={s(rec[21:25])} f19={s(rec[25:27])}")

if __name__ == "__main__": main()
