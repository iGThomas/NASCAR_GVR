"""Test-BytePatches.py - check the installer's patch table (developer tool).

Applies Patches\\nascar-bytepatches.txt to the OEM files of the extracted disc and checks the
result is byte-identical to a known-good patched install. Run it after Make-BytePatches.py.

Usage: python Test-BytePatches.py <payload File_Group> <working install> [table]
"""
import os
import sys


def find(root, rel):
    """case-insensitive path lookup (the disc and the install differ in case)"""
    cur = root
    for part in rel.replace("\\", "/").split("/"):
        cur = os.path.join(cur, [e for e in os.listdir(cur) if e.lower() == part.lower()][0])
    return cur


def main():
    payload, install = sys.argv[1], sys.argv[2]
    table = sys.argv[3] if len(sys.argv) > 3 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "Patches", "nascar-bytepatches.txt")
    files, cur = {}, None
    for line in open(table):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("file "):
            cur = line[5:]
            files[cur] = []
            continue
        off, old, new = line.split()
        files[cur].append((int(off, 16), bytes.fromhex(old), bytes.fromhex(new)))
    ok = 0
    for rel, patches in files.items():
        data = bytearray(open(find(os.path.join(payload, "NASCAR"), rel), "rb").read())
        for off, old, new in patches:
            if data[off:off + len(old)] != old:
                print("OEM bytes differ:", rel, hex(off))
                break
            data[off:off + len(new)] = new
        else:
            if bytes(data) == open(find(install, rel), "rb").read():
                ok += 1
                continue
            print("result differs:", rel)
    print("%d of %d files reproduced exactly" % (ok, len(files)))
    sys.exit(0 if ok == len(files) else 1)


if __name__ == "__main__":
    main()
