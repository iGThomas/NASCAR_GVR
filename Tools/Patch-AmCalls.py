r"""Patch-AmCalls.py - remove scene-script calls to Gvr methods that no loaded plug-in provides.

In the Anark shell, `Gvr.Call("<method>")` on a method no plug-in registers does not raise a
catchable error: it silently aborts the whole script call chain. The removed cabinet-linking
plug-in (GvrLinkingPlugIn) provided "SendPacket", which CabinetComm.sendPacket() calls in 17
scenes; on car select that aborted DriverSelect.resetDrivers() -> updateDriver() every frame, so
the car and driver name never appeared and the 24 s timer picked a car (NASCAR_FINDINGS.md 11).

The statement `Gvr.Call( "<method>", ... );` is replaced by an empty statement `;` (a comment
would be longer than the call and may not refit a record with no slack). Records are rewritten at
their exact original compressed size (see Patch-AmPaths.py), so nothing else in the file moves.

Usage:
  python Patch-AmCalls.py --method SendPacket <Shell folder or .am files...>
Patches files IN PLACE and keeps <file>.am.oem only if no .oem exists yet (so a file already
patched by Patch-AmPaths keeps its pristine original). Use --dry-run to list what would change.
"""
import argparse, glob, importlib.util, os, re, shutil, struct, sys, zlib

_spec = importlib.util.spec_from_file_location(
    "pap", os.path.join(os.path.dirname(os.path.abspath(__file__)), "Patch-AmPaths.py"))
pap = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(pap)


def patch(path: str, method: str, dry: bool) -> int:
    data = bytearray(open(path, "rb").read())
    call_re = re.compile(rb'Gvr\.Call\(\s*"' + re.escape(method.encode()) + rb'"[^;\r\n]*\);')
    note = b";"   # an empty statement: always shorter than the call, so the record always refits
    total = 0
    for m in list(re.finditer(rb"ANRK\x00\x00\x00\x00", bytes(data))):
        a = m.start(); s = a + 12
        tag, rec_len = struct.unpack_from("<HI", data, a - 10)
        unc = struct.unpack_from("<I", data, a + 8)[0]
        if tag != 0x1770 or rec_len != unc + 22:
            continue
        o = zlib.decompressobj(); text = o.decompress(bytes(data[s:s + unc * 2 + 1_000_000]))
        clen = len(data[s:s + unc * 2 + 1_000_000]) - len(o.unused_data)
        new_text, n = call_re.subn(lambda _m: note, text)
        if not n:
            continue
        total += n
        print(f"  record {a:#x}: {n} call(s)")
        if dry:
            continue
        stream = pap.deflate_exact(new_text, bytes(data[s:s + 2]), clen)
        if stream is None:
            sys.exit(f"  record {a:#x}: cannot refit")
        chk = zlib.decompressobj()
        assert chk.decompress(stream) == new_text and chk.eof and not chk.unused_data
        data[s:s + clen] = stream
        struct.pack_into("<I", data, a + 8, len(new_text))
        struct.pack_into("<I", data, a - 8, len(new_text) + 22)
    if total and not dry:
        if not os.path.exists(path + ".oem"):
            shutil.copyfile(path, path + ".oem")
        open(path, "wb").write(data)
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("targets", nargs="+")
    ap.add_argument("--method", required=True)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    files = []
    for t in a.targets:
        files += glob.glob(os.path.join(t, "**", "*.am"), recursive=True) if os.path.isdir(t) else [t]
    grand = 0
    for f in sorted(files):
        n = patch(f, a.method, a.dry_run)
        if n:
            print(f"{f}: {n} call(s) {'would be ' if a.dry_run else ''}removed")
            grand += n
    print(f"total: {grand}")


if __name__ == "__main__":
    main()
