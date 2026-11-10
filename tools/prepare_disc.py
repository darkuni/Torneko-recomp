#!/usr/bin/env python3
"""Extract the recompiler inputs from your World of Dragon Warrior: Torneko - The Last Hope (NTSC-U, SLUS-01181) disc image.

    python3 tools/prepare_disc.py Torneko.cue game      (or a .chd: needs chdman on PATH)

The game folder gets the executable and the files the code modules come from.
Only used to build the port; players just run the exe with their disc image.
"""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

EXE_SHA1 = {
    'SLUS_011.81': '80b7c17df5c3f95e06638f2ca518dfb49a97a19b',
}
FILES = ['SLUS_011.81', 'DISK_DG.CD', 'DISK_ED.CD', 'DISK_MK.CD', 'DISK_MM.CD']


class Iso:
    def __init__(self, path):
        self.f = open(path, 'rb')

    def sector(self, lba):
        self.f.seek(lba * 2352)
        return self.f.read(2352)[24:24 + 2048]

    def read(self, lba, size):
        return b''.join(self.sector(lba + i) for i in range((size + 2047) // 2048))[:size]

    def root_files(self):
        """All files on the disc, by path ("BGDATA/RM_PACK.BIN")."""
        pvd = self.sector(16)
        assert pvd[1:6] == b'CD001', 'not an ISO 9660 data track'
        rec = pvd[156:190]
        out = {}

        def walk(lba, size, path):
            data = self.read(lba, size)
            p = 0
            while p < size:
                n = data[p]
                if n == 0:
                    p = (p // 2048 + 1) * 2048
                    continue
                r = data[p:p + n]
                p += n
                name = r[33:33 + r[32]]
                if name in (b'\0', b'\1'):
                    continue
                name = name.decode().split(';')[0]
                el, es = struct.unpack_from('<I', r, 2)[0], struct.unpack_from('<I', r, 10)[0]
                if r[25] & 2:
                    walk(el, es, path + name + '/')
                else:
                    out[path + name] = (el, es)

        walk(struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0], '')
        return out


def main():
    src, out = sys.argv[1], sys.argv[2]
    tmp = None
    if src.lower().endswith('.chd'):
        tmp = tempfile.mkdtemp()
        subprocess.check_call(['chdman', 'extractcd', '-i', src, '-o', os.path.join(tmp, 'd.cue'),
                               '-ob', os.path.join(tmp, 'd.bin')], stdout=subprocess.DEVNULL)
        bin_path = os.path.join(tmp, 'd.bin')
    elif src.lower().endswith('.cue'):
        line = [l for l in open(src) if l.strip().startswith('FILE')][0]
        bin_path = os.path.join(os.path.dirname(src), line.split('"')[1])
    else:
        bin_path = src
    iso = Iso(bin_path)
    files = iso.root_files()
    os.makedirs(out, exist_ok=True)
    for name in FILES:
        lba, size = files[name]
        data = iso.read(lba, size)
        if name in EXE_SHA1:
            h = hashlib.sha1(data).hexdigest()
            assert h == EXE_SHA1[name], '%s: unexpected SHA1 %s (needs the NTSC-U release, SLUS-01181)' % (name, h)
        os.makedirs(os.path.dirname(os.path.join(out, name)), exist_ok=True)
        open(os.path.join(out, name), 'wb').write(data)
        print('%-12s %9d bytes' % (name, size))
    # code overlays: entries of the BINPACK archives (config/images.txt)
    cfg = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'config', 'images.txt')
    os.makedirs(os.path.join(out, 'overlays'), exist_ok=True)
    n = 0
    for line in open(cfg):
        p = line.split('#')[0].split()
        if len(p) == 7 and p[0] == 'ovl':
            arc = open(os.path.join(out, p[4]), 'rb').read()
            off, size = int(p[5], 16), int(p[6], 16)
            open(os.path.join(out, p[1]), 'wb').write(arc[off:off + size])
            n += 1
    print('%d overlays' % n)
    if tmp:
        os.remove(bin_path)


if __name__ == '__main__':
    main()
