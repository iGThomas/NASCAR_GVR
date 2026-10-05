r"""Edit-AmScript.py - edit the text of one compressed script record in an Anark .am, keeping its
compressed size (so nothing else in the file moves). Used for the checkpoint output() lines that
located the car-select abort (NASCAR_FINDINGS.md section 12).

usage: Edit-AmScript.py <file.am> <record-marker> <find-file> <replace-file> [--strip-comments]
  record-marker : a string that identifies the record (e.g. BEHAVIOR NAME="DriverSelect_Behavior")
  find/replace  : files holding the exact text to find (must occur once) and its replacement
  --strip-comments : drop full-line // comments in that script to make room for added text
  The marker must not contain double quotes when passed from PowerShell (they are stripped).
Works on the file in place (callers keep their own backup).
"""
import re, struct, sys, zlib, importlib.util

import os
spec = importlib.util.spec_from_file_location("pap", os.path.join(os.path.dirname(os.path.abspath(__file__)), "Patch-AmPaths.py"))
pap = importlib.util.module_from_spec(spec); spec.loader.exec_module(pap)

path, marker = sys.argv[1], sys.argv[2].encode("latin1")
find = open(sys.argv[3], "rb").read().replace(b"\r\n", b"\n")
repl = open(sys.argv[4], "rb").read().replace(b"\r\n", b"\n")
data = bytearray(open(path, "rb").read())
done = False
for m in list(re.finditer(rb"ANRK\x00\x00\x00\x00", bytes(data))):
    a = m.start(); s = a + 12
    tag, rec_len = struct.unpack_from("<HI", data, a - 10)
    unc = struct.unpack_from("<I", data, a + 8)[0]
    if tag != 0x1770 or rec_len != unc + 22:
        continue
    o = zlib.decompressobj(); text = o.decompress(bytes(data[s:s + unc * 2 + 1_000_000]))
    clen = len(data[s:s + unc * 2 + 1_000_000]) - len(o.unused_data)
    if marker not in text:
        continue
    t = text.replace(b"\r\n", b"\n")
    crlf = b"\r\n" in text
    if t.count(find) != 1:
        sys.exit(f"find text occurs {t.count(find)} times in record {a:#x}")
    t = t.replace(find, repl)
    if "--strip-comments" in sys.argv:
        # drop full-line // comments inside the script to make room (code is unchanged)
        t = b"\n".join(l for l in t.split(b"\n") if not l.lstrip().startswith(b"//"))
    new_text = t.replace(b"\n", b"\r\n") if crlf else t
    stream = pap.deflate_exact(new_text, bytes(data[s:s + 2]), clen)
    if stream is None:
        sys.exit(f"record {a:#x}: {len(new_text)} bytes do not fit in {clen} compressed bytes")
    chk = zlib.decompressobj()
    assert chk.decompress(stream) == new_text and chk.eof and not chk.unused_data
    data[s:s + clen] = stream
    struct.pack_into("<I", data, a + 8, len(new_text))
    struct.pack_into("<I", data, a - 8, len(new_text) + 22)
    print(f"record {a:#x}: edited, {unc} -> {len(new_text)} bytes, compressed size kept at {clen}")
    done = True
    break
if not done:
    sys.exit("marker not found")
open(path, "wb").write(data)
