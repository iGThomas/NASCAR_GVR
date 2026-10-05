r"""Find-AmCabinetPaths.py - list cabinet-absolute paths (c:\NASCAR\..., C:\Projects\...) inside
every .am scene script under a folder, grouped by folder prefix.

Doubled backslashes = JavaScript string literals that the scene really uses (repoint them with
Patch-AmPaths.py). Single backslashes = AKSourceFilePath authoring metadata (harmless).
Usage: python Find-AmCabinetPaths.py <Shell folder>
"""
import re, glob, zlib, collections, sys, os

root = sys.argv[1]
BS = b'\\'
# a cabinet-absolute path: c: or C: followed by one or more backslashes, then a known root folder
pat = re.compile(rb'[cC]:(?:' + re.escape(BS) + rb')+(?:NASCAR|GVR_PGA_TOUR_2006|Projects|NASCAR_TOUR_GOLF_2006)[^"\x27<>\r\n]{0,120}')

for f in sorted(glob.glob(os.path.join(root, '**', '*.am'), recursive=True)):
    d = open(f, 'rb').read()
    per = collections.Counter()
    for m in re.finditer(rb'ANRK\x00\x00\x00\x00', d):
        s = m.start() + 12
        try:
            raw = zlib.decompressobj().decompress(d[s:s + 4_000_000])
        except zlib.error:
            continue
        for h in pat.finditer(raw):
            v = h.group().decode('latin1')
            folder = v[:v.rfind('\\') + 1] if '\\' in v else v
            per[folder] += 1
    if per:
        print(os.path.relpath(f, root))
        for k, n in per.most_common():
            print(f'    {n:3d}  {k}')
