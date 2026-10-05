#!/usr/bin/env python3
"""
Dump-Hive.py — read keys/values straight out of an offline Windows registry
hive (regf), with no need to `reg load` it (which needs admin and would
mount a 2008 XP-Embedded hive on a modern machine).

Used to mine the NASCAR cabinet's own registry out of the recovery image:
    Extracted\\RecoveryImage\\WINDOWS\\system32\\config\\{SYSTEM,SOFTWARE}

Usage:
    python Dump-Hive.py <hive> <key\\path>      dump a key (recursive)
    python Dump-Hive.py <hive> --find <text>    find value names containing text
"""
import sys, struct

REG_TYPES = {0: 'REG_NONE', 1: 'REG_SZ', 2: 'REG_EXPAND_SZ', 3: 'REG_BINARY',
             4: 'REG_DWORD', 5: 'REG_DWORD_BIG_ENDIAN', 6: 'REG_LINK',
             7: 'REG_MULTI_SZ', 11: 'REG_QWORD'}


class Hive(object):
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        if self.d[:4] != b'regf':
            raise ValueError('not a registry hive: %s' % path)
        self.base = 0x1000
        self.root = struct.unpack('<I', self.d[0x24:0x28])[0]

    def cell(self, off):
        """Return the payload bytes of the cell at hive-relative offset."""
        p = self.base + off
        size = struct.unpack('<i', self.d[p:p + 4])[0]
        n = -size if size < 0 else size
        return self.d[p + 4:p + n]

    # ---- nk (key) ----
    def key_name(self, nk):
        nlen = struct.unpack('<H', nk[0x48:0x4a])[0]
        flags = struct.unpack('<H', nk[0x02:0x04])[0]
        raw = nk[0x4c:0x4c + nlen]
        return raw.decode('latin-1') if (flags & 0x20) else raw.decode('utf-16le', 'replace')

    def subkeys(self, nk):
        cnt = struct.unpack('<I', nk[0x14:0x18])[0]
        off = struct.unpack('<I', nk[0x1c:0x20])[0]
        if cnt == 0 or off == 0xFFFFFFFF:
            return []
        return self._list(off)

    def _list(self, off):
        c = self.cell(off)
        sig = c[:2]
        out = []
        if sig in (b'lf', b'lh'):
            n = struct.unpack('<H', c[2:4])[0]
            for i in range(n):
                out.append(struct.unpack('<I', c[4 + i * 8:8 + i * 8])[0])
        elif sig == b'li':
            n = struct.unpack('<H', c[2:4])[0]
            for i in range(n):
                out.append(struct.unpack('<I', c[4 + i * 4:8 + i * 4])[0])
        elif sig == b'ri':
            n = struct.unpack('<H', c[2:4])[0]
            for i in range(n):
                out.extend(self._list(struct.unpack('<I', c[4 + i * 4:8 + i * 4])[0]))
        return out

    def values(self, nk):
        cnt = struct.unpack('<I', nk[0x24:0x28])[0]
        off = struct.unpack('<I', nk[0x28:0x2c])[0]
        if cnt == 0 or off == 0xFFFFFFFF:
            return []
        lst = self.cell(off)
        out = []
        for i in range(cnt):
            vo = struct.unpack('<I', lst[i * 4:i * 4 + 4])[0]
            if vo == 0xFFFFFFFF:
                continue
            out.append(self.value(self.cell(vo)))
        return out

    def value(self, vk):
        nlen = struct.unpack('<H', vk[0x02:0x04])[0]
        dlen = struct.unpack('<I', vk[0x04:0x08])[0]
        doff = struct.unpack('<I', vk[0x08:0x0c])[0]
        vtype = struct.unpack('<I', vk[0x0c:0x10])[0]
        flags = struct.unpack('<H', vk[0x10:0x12])[0]
        raw = vk[0x14:0x14 + nlen]
        name = raw.decode('latin-1') if (flags & 1) else raw.decode('utf-16le', 'replace')
        if not name:
            name = '(Default)'
        inline = bool(dlen & 0x80000000)
        n = dlen & 0x7FFFFFFF
        data = vk[0x08:0x08 + min(n, 4)] if inline else self.cell(doff)[:n]
        return name, vtype, self.decode(vtype, data)

    @staticmethod
    def decode(vtype, data):
        if vtype in (1, 2):
            return data.decode('utf-16le', 'replace').rstrip('\x00')
        if vtype == 7:
            return [s for s in data.decode('utf-16le', 'replace').split('\x00') if s]
        if vtype == 4 and len(data) >= 4:
            return struct.unpack('<I', data[:4])[0]
        if vtype == 11 and len(data) >= 8:
            return struct.unpack('<Q', data[:8])[0]
        return data.hex() if len(data) <= 64 else data[:64].hex() + '...'

    def open(self, path):
        nk = self.cell(self.root)
        if not path:
            return nk
        for part in path.replace('/', '\\').split('\\'):
            if not part:
                continue
            found = None
            for so in self.subkeys(nk):
                c = self.cell(so)
                if c[:2] == b'nk' and self.key_name(c).lower() == part.lower():
                    found = c
                    break
            if found is None:
                return None
            nk = found
        return nk

    def dump(self, nk, prefix='', depth=0, maxdepth=3):
        for name, vtype, val in self.values(nk):
            print('%s  %-34s %-14s %s' % (prefix, name, REG_TYPES.get(vtype, str(vtype)), val))
        if depth >= maxdepth:
            return
        for so in self.subkeys(nk):
            c = self.cell(so)
            if c[:2] != b'nk':
                continue
            kn = self.key_name(c)
            print('%s[%s]' % (prefix, kn))
            self.dump(c, prefix + '  ', depth + 1, maxdepth)

    def find(self, needle, nk=None, path='', hits=None, depth=0):
        if hits is None:
            hits = []
        if nk is None:
            nk = self.cell(self.root)
        if depth > 12:
            return hits
        for name, vtype, val in self.values(nk):
            if needle.lower() in name.lower():
                hits.append((path, name, REG_TYPES.get(vtype, str(vtype)), val))
        for so in self.subkeys(nk):
            c = self.cell(so)
            if c[:2] != b'nk':
                continue
            self.find(needle, c, path + '\\' + self.key_name(c), hits, depth + 1)
        return hits


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    h = Hive(sys.argv[1])
    if sys.argv[2] == '--find':
        for p, n, t, v in h.find(sys.argv[3]):
            print('%s\n    %s = [%s] %s' % (p, n, t, v))
    else:
        k = h.open(sys.argv[2])
        if k is None:
            print('key not found: %s' % sys.argv[2])
            sys.exit(2)
        md = int(sys.argv[3]) if len(sys.argv) > 3 else 3
        h.dump(k, maxdepth=md)
