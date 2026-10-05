r"""Find-AmMissingCalls.py - list scene-script Gvr.Call("<method>") names no loaded plug-in provides.

In the Anark shell a Gvr.Call to a method that no plug-in registers does NOT raise a catchable
error - it silently aborts the whole script call chain, so a slide just sits there (car select
showed only "Start Your Engines"; the operator Info page stuck on "Loading. Please Wait...").
Run this after changing the plug-in list and fix every hit that is reachable
(Patch-AmCalls.py). Commented-out calls are reported too - check the context before patching.

Usage: python Find-AmMissingCalls.py <Shell folder> [plugin names that are loaded ...]
Default loaded set = Game\config\GvrShellPlugInList.xml as shipped minus GvrLinkingPlugIn and
GvrMotionPlugIn (removed for a desktop install).
"""
import collections, glob, os, re, sys, zlib

root = sys.argv[1] if len(sys.argv) > 1 else r"D:\Games\NASCAR\Shell"
loaded = sys.argv[2:] or ["ZeusIOPlugIn", "HerculesPlugIn", "GvrDiagnosticsPlugIn", "NASCARPlugIn",
                          "GvrMusicPlugIn", "GvrDirtyWordPlugIn", "GvrPlusDEPlugin"]
plug_dir = os.path.join(root, "bin", "plugins")
all_plugins = [os.path.splitext(f)[0] for f in os.listdir(plug_dir)
               if f.lower().endswith(".dll") and "_oem" not in f.lower()]
blobs = {p: open(os.path.join(plug_dir, p + ".dll"), "rb").read() for p in all_plugins}
amp = open(os.path.join(root, "bin", "AMPlayer.exe"), "rb").read()


def providers(name):
    n, w = name.encode() + b"\0", name.encode("utf-16le") + b"\0\0"
    hits = [p for p, d in blobs.items() if n in d or w in d]
    if n in amp or w in amp:
        hits.append("AMPlayer")
    return hits


call_re = re.compile(rb'Gvr\.Call\(\s*"([A-Za-z_0-9]+)"')
uses = collections.defaultdict(set)
for f in glob.glob(os.path.join(root, "**", "*.am"), recursive=True):
    d = open(f, "rb").read()
    for m in re.finditer(rb"ANRK\x00\x00\x00\x00", d):
        s = m.start() + 12
        try:
            t = zlib.decompressobj().decompress(d[s:s + 4_000_000])
        except zlib.error:
            continue
        beh = re.search(rb'<BEHAVIOR NAME="([^"]+)"', t)
        for c in call_re.finditer(t):
            uses[c.group(1).decode()].add(f"{os.path.relpath(f, root)}:{beh.group(1).decode() if beh else '?'}")

for name in sorted(uses):
    p = providers(name)
    if not any(x in loaded or x == "AMPlayer" for x in p):
        print(f"{name:38} provided by {p or 'NOBODY'}")
        for u in sorted(uses[name]):
            print(f"      {u}")
