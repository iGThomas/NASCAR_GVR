"""Extract-AmScripts.py - inflate the zlib streams embedded in an Anark .am presentation.

Anark AMPlayer scenes (Shell.am, NASCAR_Attract.am, ...) store their JavaScript behaviours
(<BEHAVIOR NAME=...><SCRIPT TYPE="JavaScript">) as zlib streams. Inflating them recovers the
scene logic verbatim: slide names, the startup state machine, every Gvr.Call and the exact
values it compares against. See NASCAR_FINDINGS.md section 11.1.

Usage: python Extract-AmScripts.py <file.am> [out_dir]
Writes <out_dir>/<stem>_<offset>.txt for every stream that inflates to >= 64 bytes and prints
the BEHAVIOR name of each.
"""
import os, re, sys, zlib

def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(src)[0] + "_scripts"
    os.makedirs(out, exist_ok=True)
    data = open(src, "rb").read()
    stem = os.path.splitext(os.path.basename(src))[0]
    n = 0
    for m in re.finditer(rb"\x78[\x01\x5e\x9c\xda]", data):
        try:
            text = zlib.decompressobj().decompress(data[m.start():m.start() + 4_000_000])
        except zlib.error:
            continue
        if len(text) < 64:
            continue
        path = os.path.join(out, f"{stem}_{m.start():x}.txt")
        open(path, "wb").write(text)
        name = re.search(rb'<BEHAVIOR NAME="([^"]+)"', text)
        print(f"0x{m.start():08x}  {len(text):8d}  {name.group(1).decode() if name else '-'}")
        n += 1
    print(f"{n} stream(s) -> {out}")

if __name__ == "__main__":
    main()
