r"""Patch-AmPaths.py - repoint the cabinet's absolute paths inside Anark .am presentations.

The NASCAR shell's scene scripts hardcode the cabinet install root as JavaScript string literals,
e.g. "c:\\NASCAR\\Shell\\Textures\\CarSkin8.jpg" in NASCAR_Selection.am (car skins and driver
names on the car-select slide). On a portable install those files do not exist, the slide shows
only its background, and its 24-second timeout auto-picks a car. This tool rewrites those
literals to the real install root.

.am container facts (NASCAR_FINDINGS.md section 11):
  * tag-length-value records; compressed script records are tag 0x1770 whose LE32 length is the
    UNCOMPRESSED size + 22, followed by 4 zero bytes, 'ANRK', 4 zero bytes, LE32 uncompressed
    size, then a zlib stream;
  * enclosing records store PHYSICAL byte lengths, and there is no compressed-size field.
So a rewritten record must keep exactly the same compressed byte length. We get that by
deflating most of the text normally and emitting the tail as a raw "stored" deflate block whose
size is chosen to make the total match. Only the record's two uncompressed-size fields change.

Only JS literals (doubled backslashes) are rewritten; single-backslash metadata such as
AKSourceFilePath "file://C:\Projects\..." is left alone.

Usage:
  python Patch-AmPaths.py --new-root "D:\Games\NASCAR" <file.am> [...]
  python Patch-AmPaths.py --map "c:/NASCAR/Shell/Textures/=Textures/" --map "c:/NASCAR/Shell/Audio=../Audio" NASCAR_Selection.am
      (verified mapping for the car-select scene: textures relative to the .am folder, music
       relative to Shell\bin; shorter than the cabinet path, so it always fits)
  python Patch-AmPaths.py --restore <file.am> [...]
Keeps the OEM file as <file>.am.oem and always patches from it (idempotent).
"""
import argparse, os, re, shutil, struct, sys, zlib

BS = b"\\"
DBS = BS + BS                                     # a doubled backslash as it appears in JS source
OLD_ROOT_RE = re.compile(rb"[cC]:" + re.escape(DBS) + rb"NASCAR" + re.escape(DBS))
TAG_COMPRESSED = 0x1770


def js_root(new_root: str) -> bytes:
    r = new_root.rstrip("\\/").replace("/", "\\")
    return r.encode("latin1").replace(BS, DBS) + DBS


def js_literal(path_prefix: str) -> bytes:
    """A path prefix as it appears inside a JS string literal (backslashes doubled)."""
    return path_prefix.replace("/", "\\").encode("latin1").replace(BS, DBS)


def deflate_exact(text: bytes, header: bytes, target: int):
    """Return a zlib stream (with the given 2-byte header) of exactly `target` bytes, or None.

    Tried in order of overhead: (1) a plain stream from some level/memLevel/strategy combination
    that happens to be exactly the right size; (2) one sync-flush split (5 bytes overhead) at a
    chosen position; (3) a normal stream for the head plus a raw stored block for the tail."""
    n = len(text)
    adler = struct.pack(">I", zlib.adler32(text) & 0xFFFFFFFF)
    body_target = target - len(header) - len(adler)
    combos = [(lv, ml, st) for lv in range(9, 0, -1) for ml in range(9, 0, -1)
              for st in (zlib.Z_DEFAULT_STRATEGY, zlib.Z_FILTERED)]
    best = {}
    for lv, ml, st in combos:                                             # (1)
        c = zlib.compressobj(lv, zlib.DEFLATED, -15, ml, st)
        body = c.compress(text) + c.flush()
        if len(body) == body_target:
            return header + body + adler
        best[(lv, ml, st)] = len(body)
    hit = _stored_tail(text, header, adler, target)                     # (3) fast, needs slack
    if hit:
        return hit
    for (lv, ml, st), size in sorted(best.items(), key=lambda kv: kv[1])[:6]:   # (2) slow
        if size + 5 > body_target + 40:
            continue
        for k in range(64, n - 64, max(1, n // 1500)):
            c = zlib.compressobj(lv, zlib.DEFLATED, -15, ml, st)
            body = c.compress(text[:k]) + c.flush(zlib.Z_SYNC_FLUSH) + c.compress(text[k:]) + c.flush()
            if len(body) == body_target:
                return header + body + adler
    return None


def _stored_tail(text: bytes, header: bytes, adler: bytes, target: int):
    """Deflate text[:k] normally, then emit text[k:] as a final stored block; pick k so the
    whole stream is exactly `target` bytes. Fails only when there is no slack at all."""
    n = len(text)
    for level in (9, 8, 7, 6, 5, 4, 3, 2, 1):                             # (3)
        prev = None
        for tail in range(0, min(n, 60000)):
            k = n - tail
            c = zlib.compressobj(level, zlib.DEFLATED, -15)
            body = c.compress(text[:k]) + c.flush(zlib.Z_SYNC_FLUSH)   # byte-aligned
            stored = bytes([1]) + struct.pack("<HH", tail, tail ^ 0xFFFF) + text[k:]   # final stored block
            size = len(header) + len(body) + len(stored) + len(adler)
            if size == target:
                return header + body + stored + adler
            if size > target and prev is not None and prev > target:
                break                      # only growing from here at this level
            prev = size
    return None


def patch_file(path: str, old_re, rep: bytes) -> int:
    oem = path + ".oem"
    src = oem if os.path.exists(oem) else path
    data = bytearray(open(src, "rb").read())
    changed = 0
    for m in list(re.finditer(rb"ANRK\x00\x00\x00\x00", bytes(data))):
        a = m.start()
        s = a + 12
        tag, rec_len = struct.unpack_from("<HI", data, a - 10)
        unc = struct.unpack_from("<I", data, a + 8)[0]
        if tag != TAG_COMPRESSED or rec_len != unc + 22:
            continue
        o = zlib.decompressobj()
        text = o.decompress(bytes(data[s:s + unc * 2 + 1_000_000]))
        clen = len(data[s:s + unc * 2 + 1_000_000]) - len(o.unused_data)
        if len(text) != unc or not o.eof:
            print(f"  skip record at {a:#x}: unexpected size")
            continue
        new_text, n = text, 0
        for o_re, r in (old_re if isinstance(old_re, list) else [(old_re, rep)]):
            # function replacement: a plain replacement string would have its backslashes
            # treated as regex escapes, collapsing the doubled JS backslashes to single ones
            new_text, k = o_re.subn(lambda _m, r=r: r, new_text)
            n += k
        if not n:
            continue
        stream = deflate_exact(new_text, bytes(data[s:s + 2]), clen)
        if stream is None:
            sys.exit(f"  record at {a:#x}: cannot fit {len(new_text)} bytes into {clen} compressed bytes")
        chk = zlib.decompressobj()
        if chk.decompress(stream) != new_text or not chk.eof or chk.unused_data:
            sys.exit(f"  record at {a:#x}: verification failed")
        data[s:s + clen] = stream
        struct.pack_into("<I", data, a + 8, len(new_text))
        struct.pack_into("<I", data, a - 8, len(new_text) + 22)
        changed += n
        print(f"  record {a:#x}: {n} path(s) repointed, {unc} -> {len(new_text)} bytes, compressed size kept at {clen}")
    if changed:
        if not os.path.exists(oem):
            shutil.copyfile(path, oem)
        open(path, "wb").write(data)
    return changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--new-root", help="install root that replaces the cabinet's c:\\NASCAR")
    ap.add_argument("--map", metavar="OLD=NEW", action="append",
                    help="replace path prefix OLD (case-insensitive) with NEW; repeatable, applied in "
                         "order. Relative paths: Anark resolves remoteSource against the .am's folder "
                         "(Shell\\), the native music plug-in against the working directory (Shell\\bin). "
                         "Forward slashes are converted. Use this when --new-root does not fit.")
    ap.add_argument("--restore", action="store_true")
    a = ap.parse_args()
    rep = None
    if a.map:
        old_re = []
        for m in a.map:
            old, new = m.split("=", 1)
            old_re.append((re.compile(re.escape(js_literal(old)), re.IGNORECASE), js_literal(new)))
    elif a.new_root:
        old_re, rep = OLD_ROOT_RE, js_root(a.new_root)
    for f in a.files:
        if a.restore:
            if os.path.exists(f + ".oem"):
                shutil.copyfile(f + ".oem", f); os.remove(f + ".oem"); print("restored", f)
            continue
        if not (a.map or a.new_root):
            sys.exit("--new-root or --map is required")
        print(f)
        n = patch_file(f, old_re, rep)
        print(f"  {n} literal(s) repointed" if n else "  nothing to repoint")


if __name__ == "__main__":
    main()
