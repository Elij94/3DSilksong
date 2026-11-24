"""Minimal replacement for devkitPro's mkromfs3ds: builds a raw level-3 RomFS image
(the format libctru's romfsMountFromFile reads).

usage: python mkromfs.py <folder> <output.romfs>
       python mkromfs.py --verify <folder> <output.romfs>
"""
import os
import struct
import sys

INVALID = 0xFFFFFFFF
HEADER_SIZE = 0x28


def align(n, a):
    return (n + a - 1) // a * a


def utf16(name):
    return name.encode("utf-16-le")


def calc_hash(parent, name_bytes, count):
    h = parent ^ 123456789
    for i in range(0, len(name_bytes), 2):
        unit = name_bytes[i] | (name_bytes[i + 1] << 8)
        h = ((h >> 5) | (h << 27)) & 0xFFFFFFFF
        h ^= unit
    return h % count


def hash_count(n):
    if n < 3:
        return 3
    if n < 19:
        return n | 1
    c = n
    while any(c % d == 0 for d in (2, 3, 5, 7, 11, 13, 17)):
        c += 1
    return c


class Dir:
    def __init__(self, name, path, parent):
        self.name, self.path, self.parent = name, path, parent
        self.dirs, self.files = [], []
        self.off = 0


class File:
    def __init__(self, name, path, parent):
        self.name, self.path, self.parent = name, path, parent
        self.off = 0
        self.size = os.path.getsize(path)
        self.data_off = 0


def scan(root):
    top = Dir("", root, None)
    stack = [top]
    alld, allf = [], []
    while stack:
        d = stack.pop(0)
        alld.append(d)
        entries = sorted(os.listdir(d.path))
        for e in entries:
            p = os.path.join(d.path, e)
            if os.path.isdir(p):
                d.dirs.append(Dir(e, p, d))
            else:
                f = File(e, p, d)
                d.files.append(f)
        stack.extend(d.dirs)
    # files in directory order, so each directory's files are contiguous
    for d in alld:
        allf.extend(d.files)
    return top, alld, allf


def entry_len(name, base):
    return base + align(len(utf16(name)), 4)


def build(root, out):
    top, alld, allf = scan(root)
    off = 0
    for d in alld:
        d.off = off
        off += entry_len(d.name, 0x18)
    dir_table_size = off
    off = 0
    for f in allf:
        f.off = off
        off += entry_len(f.name, 0x20)
    file_table_size = off
    data = 0
    for f in allf:
        data = align(data, 0x10)
        f.data_off = data
        data += f.size

    dcount, fcount = hash_count(len(alld)), hash_count(len(allf))
    dhash, fhash = [INVALID] * dcount, [INVALID] * fcount
    dnext, fnext = {}, {}
    for d in alld:
        pofs = d.parent.off if d.parent else 0
        h = calc_hash(pofs, utf16(d.name), dcount)
        dnext[d.off] = dhash[h]
        dhash[h] = d.off
    for f in allf:
        h = calc_hash(f.parent.off, utf16(f.name), fcount)
        fnext[f.off] = fhash[h]
        fhash[h] = f.off

    dir_tab = bytearray()
    for d in alld:
        sib = INVALID
        if d.parent:
            i = d.parent.dirs.index(d)
            if i + 1 < len(d.parent.dirs):
                sib = d.parent.dirs[i + 1].off
        n = utf16(d.name)
        dir_tab += struct.pack("<6I", d.parent.off if d.parent else 0, sib,
                               d.dirs[0].off if d.dirs else INVALID,
                               d.files[0].off if d.files else INVALID,
                               dnext[d.off], len(n))
        dir_tab += n + b"\0" * (align(len(n), 4) - len(n))
    file_tab = bytearray()
    for f in allf:
        i = f.parent.files.index(f)
        sib = f.parent.files[i + 1].off if i + 1 < len(f.parent.files) else INVALID
        n = utf16(f.name)
        file_tab += struct.pack("<IIQQII", f.parent.off, sib, f.data_off, f.size, fnext[f.off], len(n))
        file_tab += n + b"\0" * (align(len(n), 4) - len(n))
    assert len(dir_tab) == dir_table_size and len(file_tab) == file_table_size

    o = HEADER_SIZE
    dh_off = o; o += 4 * dcount
    dt_off = o; o += dir_table_size
    fh_off = o; o += 4 * fcount
    ft_off = o; o += file_table_size
    fd_off = align(o, 0x10)
    header = struct.pack("<10I", HEADER_SIZE, dh_off, 4 * dcount, dt_off, dir_table_size,
                         fh_off, 4 * fcount, ft_off, file_table_size, fd_off)
    with open(out, "wb") as w:
        w.write(header)
        w.write(struct.pack("<%dI" % dcount, *dhash))
        w.write(dir_tab)
        w.write(struct.pack("<%dI" % fcount, *fhash))
        w.write(file_tab)
        w.write(b"\0" * (fd_off - o))
        pos = 0
        for f in allf:
            w.write(b"\0" * (f.data_off - pos))
            with open(f.path, "rb") as r:
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        break
                    w.write(chunk)
            pos = f.data_off + f.size
    print("%s: %d dirs, %d files, %.1f MB" % (out, len(alld), len(allf), os.path.getsize(out) / 1e6))


def verify(root, img):
    """Look every file up the way libctru does (hash chains), compare bytes."""
    with open(img, "rb") as r:
        blob = r.read()
    hdr = struct.unpack_from("<10I", blob, 0)
    _, dh_off, dh_size, dt_off, _, fh_off, fh_size, ft_off, _, fd_off = hdr
    dcount, fcount = dh_size // 4, fh_size // 4

    def dname(off):
        ln = struct.unpack_from("<I", blob, dt_off + off + 0x14)[0]
        return blob[dt_off + off + 0x18: dt_off + off + 0x18 + ln]

    def fname(off):
        ln = struct.unpack_from("<I", blob, ft_off + off + 0x1C)[0]
        return blob[ft_off + off + 0x20: ft_off + off + 0x20 + ln]

    def find_dir(parent, name):
        n = utf16(name)
        cur = struct.unpack_from("<I", blob, dh_off + 4 * calc_hash(parent, n, dcount))[0]
        while cur != INVALID:
            if struct.unpack_from("<I", blob, dt_off + cur)[0] == parent and dname(cur) == n:
                return cur
            cur = struct.unpack_from("<I", blob, dt_off + cur + 0x10)[0]
        raise KeyError(name)

    def find_file(parent, name):
        n = utf16(name)
        cur = struct.unpack_from("<I", blob, fh_off + 4 * calc_hash(parent, n, fcount))[0]
        while cur != INVALID:
            if struct.unpack_from("<I", blob, ft_off + cur)[0] == parent and fname(cur) == n:
                return cur
            cur = struct.unpack_from("<I", blob, ft_off + cur + 0x18)[0]
        raise KeyError(name)

    n = 0
    for dp, _, files in os.walk(root):
        rel = os.path.relpath(dp, root)
        d = 0
        if rel != ".":
            for part in rel.split(os.sep):
                d = find_dir(d, part)
        for fn in files:
            fo = find_file(d, fn)
            doff, size = struct.unpack_from("<QQ", blob, ft_off + fo + 8)
            with open(os.path.join(dp, fn), "rb") as r:
                if blob[fd_off + doff: fd_off + doff + size] != r.read():
                    raise SystemExit("MISMATCH " + os.path.join(dp, fn))
            n += 1
    print("verify ok: %d files" % n)


if __name__ == "__main__":
    if sys.argv[1] == "--verify":
        verify(sys.argv[2], sys.argv[3])
    else:
        build(sys.argv[1], sys.argv[2])
